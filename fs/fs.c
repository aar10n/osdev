//
// Created by Aaron Gill-Braun on 2020-10-30.
//

#include <fs.h>
#include <vfs.h>
#include <printf.h>
#include <panic.h>
#include <mm/heap.h>
#include <errno.h>

#include <inode.h>
#include <dirent.h>
#include <path.h>
#include <device.h>
#include <process.h>
#include <atomic.h>

static dev_t __dev_id = 0;

//
// Filesystem API
//

// 0x8000
// 0x9000

// 0x9000
// 0x1000
//

void fs_init() {
  kprintf("[fs] initializing...\n");

  path_init();
  vfs_init();

  fs_register_device(NULL, &pseudo_impl);
  current_process->pwd = fs_root;

  kprintf("[fs] done!\n");
}

//

int fs_mount(fs_driver_t *driver, const char *device, const char *path) {
  kprintf("[fs] mount\n");
  fs_node_t *dev_node;
  NOT_NULL(dev_node = vfs_get_node(str_to_path(device), 0));

  if (!IS_IFBLK(dev_node->mode)) {
    errno = ENOTBLK;
    return -1;
  } else if (strcmp(path, "/") == 0) {
    errno = EACCES;
    return -1;
  }

  path_t p = str_to_path(path);
  fs_node_t *parent;
  NOT_NULL(parent = vfs_get_node(path_dirname(p), O_DIRECTORY));

  fs_device_t *dev = dev_node->ptr1;

  dev_t dev_id = atomic_fetch_add(&__dev_id, 1);
  fs_device_t *copy = kmalloc(sizeof(fs_device_t));
  memcpy(dev, copy, sizeof(fs_device_t));
  copy->id = dev_id;

  fs_node_t *mount = vfs_create_node(parent, S_IFDIR | S_IFMNT);
  mount->dev = dev_id;
  // mount->ifmnt.shadow = NULL;

  path_t basename = path_basename(p);
  char name[p_len(basename) + 1];
  pathcpy(name, basename);

  fs_node_t *child = vfs_find_child(parent, basename);
  if (child == NULL) {
    vfs_add_node(parent, mount, name);
  } else {
    vfs_swap_node(child, mount);
  }

  fs_t *instance = driver->impl->mount(copy, mount);
  if (instance == NULL) {
    kfree(mount);
    return -1;
  }

  return 0;
}

int fs_unmount(const char *path) {
  kprintf("[fs] unmount\n");
  fs_node_t *mount;
  NOT_NULL(mount = vfs_get_node(str_to_path(path), 0));

  if (!IS_IFMNT(mount->mode)) {
    errno = ENOTMNT;
    return -1;
  }

  fs_t *instance = mount->fs;

  // sync all data before unmounting
  if (instance->driver->impl->sync(instance) < 0) {
    return -1;
  }
  if (instance->impl->unmount(instance, mount) < 0) {
    return -1;
  }

  if (mount->ptr1 == NULL) {
    vfs_remove_node(mount);
  } else {
    vfs_swap_node(mount, mount->ptr1);
  }

  kfree(instance);
  return 0;
}

//

int fs_open(const char *filename, int flags, mode_t mode) {
  kprintf("[fs] open\n");
  fs_node_t *node = vfs_get_node(str_to_path(filename), flags);
  if (node == NULL && errno != ENOENT && !(flags & O_CREAT)) {
    return -1;
  } else if (node != NULL && flags & O_CREAT && flags & O_EXCL) {
    errno = EEXIST;
    return -1;
  }

  path_t path = str_to_path(filename);
  inode_t *inode = NULL;
  if (node == NULL) {
    // create the node
    path_t dirname = path_dirname(path);
    fs_node_t *parent;
    NOT_NULL(parent = vfs_get_node(dirname, 0));

    mode |= (flags & O_DIRECTORY) ? S_IFDIR : S_IFREG;

    NOT_NULL(inode = inode_create(parent->fs, mode));

    path_t basename = path_basename(path);
    char name[p_len(basename) + 1];
    pathcpy(name, basename);

    node = vfs_create_from_inode(parent, inode);
    vfs_add_node(parent, node, name);
  } else {
    NOT_NULL(inode = inode_get(node));
  }

  file_t *file;
  NOT_NULL(file = file_create(node, flags));
  return file->fd;
}

int fs_close(int fd) {
  kprintf("[fs] close\n");
  file_t *file;
  NOT_NULL(file = file_get(fd));
  file_delete(file);
  return 0;
}

//

ssize_t fs_read(int fd, void *buf, size_t nbytes) {
  kprintf("[fs] read\n");
  file_t *file;
  NOT_NULL(file = file_get(fd));
  inode_t *inode;
  NOT_NULL(inode = inode_get(file->node));
  if (file->flags & O_APPEND) {
    file->offset = inode->size;
  }

  fs_t *fs = file->node->fs;
  // aquire(file->lock);
  mutex_lock(&inode->lock);
  ssize_t nread = fs->impl->read(file->node->fs, inode, file->offset, nbytes, buf);
  mutex_unlock(&inode->lock);
  // release(file->lock);

  file->offset += nread;
  return nread;
}

ssize_t fs_write(int fd, void *buf, size_t nbytes) {
  kprintf("[fs] write\n");
  file_t *file;
  NOT_NULL(file = file_get(fd));
  inode_t *inode;
  NOT_NULL(inode = inode_get(file->node));

  fs_t *fs = file->node->fs;
  // aquire(file->lock);
  mutex_lock(&inode->lock);
  ssize_t nwritten = fs->impl->write(file->node->fs, inode, file->offset, nbytes, buf);
  mutex_unlock(&inode->lock);
  // release(file->lock);

  file->offset += nwritten;
  return nwritten;
}

off_t fs_lseek(int fd, off_t offset, int whence) {
  kprintf("[fs] lseek\n");
  file_t *file;
  NOT_NULL(file = file_get(fd));

  if (IS_IFIFO(file->node->mode)) {
    errno = ESPIPE;
    return -1;
  }

  inode_t *inode;
  NOT_NULL(inode = inode_get(file->node));

  off_t pos;
  if (whence == SEEK_SET) {
    pos = offset;
  } else if (whence == SEEK_CUR) {
    pos = file->offset + offset;
  } else if (whence == SEEK_END) {
    pos = inode->size + offset;
  } else {
    errno = EINVAL;
    return -1;
  }

  file->offset = pos;
  return 0;
}

//

int fs_link(const char *path1, const char *path2) {
  kprintf("[fs] link\n");
  path_t path = str_to_path(path2);

  fs_node_t *orig;
  NOT_NULL(orig = vfs_get_node(str_to_path(path1), 0));
  fs_node_t *parent;
  NOT_NULL(parent = vfs_get_node(path_dirname(path), 0));

  if (!IS_IFDIR(parent->mode)) {
    errno = ENOTDIR;
    return -1;
  } else if (vfs_find_child(parent, path_basename(path)) != NULL) {
    errno = EEXIST;
    return -1;
  }

  inode_t *inode;
  NOT_NULL(inode = inode_get(orig));

  fs_node_t *node = vfs_create_from_inode(parent, inode);
  node->fs = parent->fs;

  char name[MAX_FILE_NAME];
  pathcpy(name, path_basename(path));
  return vfs_add_node(parent, node, name);
}

int fs_unlink(const char *path) {
  kprintf("[fs] unlink\n");
  path_t p = str_to_path(path);
  fs_node_t *node;
  NOT_NULL(node = vfs_get_node(p, 0));

  return vfs_remove_node(node);
}

int fs_symlink(const char *path1, const char *path2) {
  kprintf("[fs] symlink\n");
  fs_node_t *orig;
  NOT_NULL(orig = vfs_get_node(str_to_path(path1), 0));

  path_t path = str_to_path(path2);
  fs_node_t *dest;
  NOT_NULL(dest = vfs_get_node(path_dirname(path), 0));

  if (!IS_IFDIR(dest->mode)) {
    errno = ENOTDIR;
    return -1;
  } else if (vfs_find_child(dest, path_basename(path)) != NULL) {
    errno = EEXIST;
    return -1;
  }

  fs_node_t *node;
  NOT_NULL(node = vfs_create_node(dest, S_IFLNK));
  path_t link_path = str_to_path(path1);
  node->ptr1 = path_to_str(link_path);

  inode_t *inode;
  NOT_NULL(inode = inode_get(node));

  char name[MAX_FILE_NAME];
  pathcpy(name, path_basename(path));
  if (vfs_add_node(dest, node, name) < 0) {
    kfree(node);
    return -1;
  }

  size_t plen = strlen(path1);
  ssize_t nwritten = node->fs->impl->write(node->fs, inode, 0, plen + 1, (char *) path1);
  if (nwritten < 0 || nwritten != plen + 1) {
    return -1;
  }

  vfs_add_link(path1, orig);
  return 0;
}

int fs_rename(const char *oldfile, const char *newfile) {
  kprintf("[fs] rename\n");

  errno = ENOSYS;
  return -1;
}

int fs_chmod(const char *path, mode_t mode) {
  kprintf("[fs] chmod\n");
  mode = mode & I_PERM_MASK;

  errno = ENOSYS;
  return -1;
}

int fs_chown(const char *path, uid_t owner, gid_t group) {
  kprintf("[fs] chown\n");
  if (owner == (uid_t) -1 && group == (gid_t) -1) {
    return 0;
  }

  errno = ENOSYS;
  return -1;
}

//

DIR *fs_opendir(const char *dirname) {
  kprintf("[fs] opendir\n");
  int flags = DIR_FILE_FLAGS;
  fs_node_t *node;
  NNOT_NULL(node = vfs_get_node(str_to_path(dirname), flags));
  file_t *file;
  NNOT_NULL(file = file_create(node, flags));
  file->node = node->ptr1;
  return file;
}

int fs_closedir(DIR *dirp) {
  kprintf("[fs] closedir\n");
  NOT_ERROR(file_exists(dirp));
  file_delete(dirp);
  return 0;
}

dirent_t *fs_readdir(DIR *dirp) {
  // kprintf("[fs] readdir\n");
  NNOT_ERROR(file_exists(dirp));
  if (dirp->flags != DIR_FILE_FLAGS) {
    errno = EBADF;
    return NULL;
  }

  if (dirp->offset == 0) {
    dirp->offset += sizeof(dirent_t);
    return dirp->node->dirent;
  } else if (dirp->node->next == NULL) {
    dirp->offset += sizeof(dirent_t);
    return NULL;
  }

  dirp->offset += sizeof(dirent_t);
  dirp->node = dirp->node->next;
  return dirp->node->dirent;
}

void fs_rewinddir(DIR *dirp) {
  if (file_exists(dirp) < 0 || dirp->flags != DIR_FILE_FLAGS) {
    return;
  }

  fs_node_t *first = dirp->node;
  while (first->prev) {
    first = first->prev;
  }

  dirp->offset = 0;
  dirp->node = first;
}

void fs_seekdir(DIR *dirp, long loc) {
  kprintf("[fs] seekdir\n");
  if (file_exists(dirp) < 0 || dirp->flags != DIR_FILE_FLAGS ||
    loc < 0 || loc % sizeof(dirent_t) != 0) {
    return;
  }

  fs_node_t *node = dirp->node;
  off_t offset = dirp->offset;
  while (offset != loc) {
    if (loc < offset) {
      offset += sizeof(dirent_t);
      if (node->next == NULL) {
        break;
      }
      node = node->next;
    } else if (loc > offset) {
      offset -= sizeof(dirent_t);
      if (node->prev == NULL) {
        break;
      }
      node = node->prev;
    }
  }

  dirp->offset = offset;
  dirp->node = node;
}

long fs_telldir(DIR *dirp) {
  kprintf("[fs] telldir\n");
  if (file_exists(dirp) < 0 || dirp->flags != DIR_FILE_FLAGS) {
    return -1;
  }
  return dirp->offset;
}

//

int fs_mkdir(const char *path, mode_t mode) {
  kprintf("[fs] mkdir\n");

  path_t p = str_to_path(path);
  fs_node_t *parent;
  NOT_NULL(parent = vfs_get_node(path_dirname(p), 0));

  path_t basename = path_basename(p);
  if (vfs_find_child(parent, basename) != NULL) {
    errno = EEXIST;
    return -1;
  }

  fs_node_t *dir;
  mode = S_IFDIR | (mode & I_PERM_MASK);
  NOT_NULL(dir = vfs_create_node(parent, mode));

  char name[MAX_FILE_NAME];
  pathcpy(name, basename);
  if (vfs_add_node(parent, dir, name) < 0) {
    vfs_remove_node(dir);
    return -1;
  }
  return 0;
}

int fs_chdir(const char *dirname) {
  kprintf("[fs] chdir\n");
  int flags = DIR_FILE_FLAGS;
  fs_node_t *node;
  NOT_NULL(node = vfs_get_node(str_to_path(dirname), flags));

  current_process->pwd = node;
  return 0;
}


