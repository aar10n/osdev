//
// Created by Aaron Gill-Braun on 2020-11-02.
//

#include <file.h>
#include <dentry.h>
#include <device.h>
#include <rb_tree.h>
#include <process.h>
#include <thread.h>
#include <panic.h>
#include <string.h>

#define FILES (PERCPU_PROCESS->files)

static rb_tree_events_t *file_tree_events = NULL;

__used void duplicate_file(rb_tree_t *tree, rb_tree_t *new_tree, rb_node_t *node, rb_node_t *new_node) {
  file_t *file = node->data;
  if (file == NULL) {
    return;
  }

  file_t *copy = kmalloc(sizeof(file_t));
  memcpy(copy, file, sizeof(file_t));
  new_node->data = copy;
}

static inline uint16_t mode_to_dirent_type(mode_t mode) {
  if (IS_IFREG(mode)) {
    return DT_REG;
  } else if (IS_IFDIR(mode)) {
    return DT_DIR;
  } else if (IS_IFBLK(mode)) {
    return DT_BLK;
  } else if (IS_IFCHR(mode)) {
    return DT_CHR;
  } else if (IS_IFLNK(mode)) {
    return DT_LNK;
  } else if (IS_IFIFO(mode)) {
    return DT_FIFO;
  } else if (IS_IFSOCK(mode)) {
    return DT_SOCK;
  }
  return DT_UNKNOWN;
}

//

file_table_t *create_file_table() {
  file_table_t *table = kmalloc(sizeof(file_table_t));
  table->fds = create_bitmap(MAX_PROC_FILES);
  table->files = create_rb_tree();

  if (file_tree_events == NULL) {
    rb_tree_events_t *events = kmalloc(sizeof(rb_tree_events_t));
    memset(events, 0, sizeof(rb_tree_events_t));
    events->duplicate_node = duplicate_file;
    file_tree_events = events;
  }

  table->files->events = file_tree_events;
  spin_init(&table->lock);
  return table;
}

file_table_t *copy_file_table(file_table_t *table) {
  file_table_t *new_table = kmalloc(sizeof(file_table_t));

  // copy the bitmap
  bitmap_t *bmp = create_bitmap(MAX_PROC_FILES);
  memcpy(bmp->map, table->fds->map, bmp->size);
  bmp->free = table->fds->free;
  bmp->used = table->fds->used;

  // copy the reb-black tree
  rb_tree_t *tree = copy_rb_tree(table->files);

  new_table->fds = bmp;
  new_table->files = tree;
  spin_init(&new_table->lock);
  return new_table;
}

//

file_t *f_alloc(dentry_t *dentry, int flags) {
  spin_lock(&FILES->lock);
  index_t fd = bitmap_get_set_free(FILES->fds);
  spin_unlock(&FILES->lock);
  if (fd < 0) {
    ERRNO = ENFILE;
    return NULL;
  }

  file_t *file = kmalloc(sizeof(file_t));
  memset(file, 0, sizeof(file_t));

  file->fd = fd;
  file->dentry = dentry;
  file->flags = flags;
  file->fd_flags = 0;
  file->mode = dentry->mode;
  file->pos = 0;
  file->ops = dentry->inode->sb->fs->file_ops;
  if (flags & O_CLOEXEC) {
    file->fd_flags |= FD_CLOEXEC;
  }

  device_t *device = locate_device(dentry->inode->dev);
  if (device == NULL && dentry->inode->dev != 0) {
    f_release(file);
    ERRNO = ENODEV;
    return NULL;
  }
  file->device = device;

  if (IS_IFCHR(dentry->mode)) {
    chrdev_t *chrdev = device->device;
    file->ops = chrdev->ops;
  } else if (IS_IFFBF(dentry->mode)) {
    framebuf_t *fb = device->device;
    file->ops = fb->ops;
  }

  spin_lock(&FILES->lock);
  rb_tree_insert(FILES->files, fd, file);
  spin_unlock(&FILES->lock);
  return file;
}

file_t *f_dup(file_t *file, int fd) {
  int new_fd = 0;
  if (fd == -1) {
    // allocate new fd
    spin_lock(&FILES->lock);
    new_fd = bitmap_get_set_free(FILES->fds);
    spin_unlock(&FILES->lock);
  } else {
    // use given fd
    spin_lock(&FILES->lock);
    kassert(bitmap_set(FILES->fds, fd) == 0);
    spin_unlock(&FILES->lock);
    new_fd = fd;
  }

  if (new_fd < 0) {
    ERRNO = EMFILE;
    return NULL;
  }

  file_t *dup = kmalloc(sizeof(file_t));
  memcpy(dup, file, sizeof(file_t));
  dup->fd = new_fd;

  spin_lock(&FILES->lock);
  rb_tree_insert(FILES->files, new_fd, file);
  spin_unlock(&FILES->lock);
  return dup;
}

void f_release(file_t *file) {
  spin_lock(&FILES->lock);
  bitmap_clear(FILES->fds, file->fd);
  rb_tree_delete(FILES->files, file->fd);
  spin_unlock(&FILES->lock);

  file->fd = -1;
  file->dentry = NULL;
  kfree(file);
}

file_t *f_locate(int fd) {
  if (fd < 0) {
    return NULL;
  }

  spin_lock(&FILES->lock);
  rb_node_t *node = rb_tree_find(FILES->files, fd);
  spin_unlock(&FILES->lock);
  if (node == NULL) {
    return NULL;
  }
  return node->data;
}

//
//
//

int f_open(file_t *file, dentry_t *dentry) {
  if (!file->ops->open) {
    return 0;
  }
  return file->ops->open(file, dentry);
}

int f_flush(file_t *file) {
  if (!file->ops->flush) {
    return 0;
  }
  return file->ops->flush(file);
}

ssize_t f_read(file_t *file, char *buf, size_t count) {
  if (!file->ops->read) {
    ERRNO = ENOTSUP;
    return -1;
  }

  ssize_t nread;
  if (IS_IFDIR(file->mode)) {
    if (count < sizeof(dirent_t)) {
      ERRNO = ENOBUFS;
      return -1;
    }

    dentry_t dentry;
    int result = file->ops->readdir(file, &dentry, true);
    if (result < 0) {
      return -1;
    }

    size_t name_len = strlen(dentry.name);
    dirent_t dirent = {
      .d_ino = dentry.ino,
      .d_off = file->pos,
      .d_reclen = sizeof(dirent_t),
      .d_type = mode_to_dirent_type(file->mode)
    };
    memcpy(dirent.d_name, dentry.name, name_len + 1);
    memcpy(buf, &dirent, sizeof(dirent_t));
    nread = sizeof(dirent_t);
  } else if (IS_IFBLK(file->mode)) {
    uint64_t lba = SIZE_TO_SECS(file->pos);
    nread = blkdev_readbuf(file->device->device, lba, count, buf);
    if (nread > 0) {
      file->pos += nread;
    }
  } else {
    nread = file->ops->read(file, buf, count, &file->pos);
  }

  if (nread < 0) {
    return -1;
  }
  return nread;
}

ssize_t f_write(file_t *file, const char *buf, size_t count) {
  if (!file->ops->write) {
    ERRNO = ENOTSUP;
    return -1;
  }

  ssize_t nwrit = file->ops->write(file, buf, count, &file->pos);
  if (nwrit < 0) {
    return -1;
  }
  return nwrit;
}

off_t f_lseek(file_t *file, off_t offset, int whence) {
  if (file->ops->lseek) {
    return file->ops->lseek(file, offset, whence);
  } else if (IS_IFIFO(file->dentry->mode)) {
    ERRNO = ESPIPE;
    return -1;
  } else if (IS_IFCHR(file->dentry->mode)) {
    return 0;
  }

  if (whence == SEEK_SET) {
    file->pos = offset;
  } else if (whence == SEEK_CUR) {
    file->pos += offset;
  } else if (whence == SEEK_END) {
    file->pos = file->dentry->inode->size + offset;
  } else {
    ERRNO = EINVAL;
    return -1;
  }
  return file->pos;
}

dentry_t *f_readdir(file_t *file) {
  if (file->ops->readdir) {
    dentry_t *next = d_alloc(file->dentry->parent, "");
    int result = file->ops->readdir(file, next, true);
    if (result < 0) {
      d_destroy(next);
      return NULL;
    }
    return next;
  }

  dentry_t *next;
  if (file->pos == 0) {
    next = LIST_FIRST(&file->dentry->children);
  } else {
    next = LIST_NEXT(file->dentry, siblings);
  }

  file->pos++;
  if (next) {
    file->dentry = next;
  }
  return next;
}

// TODO: change flags to uint32_t
int f_mmap(file_t *file, uintptr_t vaddr, size_t len, uint16_t flags) {
  if (!file->ops->mmap) {
    ERRNO = ENOTSUP;
    return -1;
  }
  return file->ops->mmap(file, vaddr, len, flags);
}
