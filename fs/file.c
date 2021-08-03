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

#define FILES (current_process->files)

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

file_t *f_alloc(dentry_t *dentry, int flags, mode_t mode) {
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
  file->mode = mode;
  file->pos = 0;
  file->ops = dentry->inode->sb->fs->file_ops;

  if (IS_IFCHR(dentry->mode)) {
    device_t *device = locate_device(dentry->inode->dev);
    if (device == NULL) {
      f_release(file);
      ERRNO = ENODEV;
      return NULL;
    }

    chrdev_t *chrdev = device->device;
    file->ops = chrdev->ops;
  } else if (IS_IFFBF(dentry->mode)) {
    device_t *device = locate_device(dentry->inode->dev);
    if (device == NULL) {
      f_release(file);
      ERRNO = ENODEV;
      return NULL;
    }

    framebuf_t *fb = device->device;
    file->ops = fb->ops;
  }

  spin_lock(&FILES->lock);
  rb_tree_insert(FILES->files, fd, file);
  spin_unlock(&FILES->lock);
  return file;
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

  ssize_t nread = file->ops->read(file, buf, count, &file->pos);
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
  } else if (IS_IFCHR(file->dentry->mode)) {
    ERRNO = ENOTSUP;
    return -1;
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

int f_mmap(file_t *file, uintptr_t vaddr, size_t len, uint16_t flags) {
  if (!file->ops->mmap) {
    ERRNO = ENOTSUP;
    return -1;
  }
  return file->ops->mmap(file, vaddr, len, flags);
}
