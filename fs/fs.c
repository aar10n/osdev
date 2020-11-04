//
// Created by Aaron Gill-Braun on 2020-10-30.
//

#include <fs.h>
#include <atomic.h>
#include <stdio.h>
#include <panic.h>
#include <mm/heap.h>
#include <errno.h>

#include <fs.h>
#include <inode.h>
#include <dirent.h>
#include <path.h>
#include <device.h>
#include <process.h>

#include <ramfs/ramfs.h>
#include <drivers/ahci.h>

#include <asm/bitmap.h>
#include <hash_table.h>
#include <murmur3.h>

// #define FILES (current->files)
#define FILES (PERCPU->files)

fs_node_t *root = NULL;
inode_table_t *inodes;
fs_node_map_t links;
fs_node_map_t cache;

uint32_t hash(char *str) {
  size_t len = strlen(str);
  uint32_t out;
  murmur_hash_x86_32(str, len, 0xDEADBEEF, &out);
  return out;
}

fs_node_t *__create_fs_node() {
  fs_node_t *node = kmalloc(sizeof(fs_node_t));
  node->inode = 0;
  node->dev = 0;
  node->mode = 0;
  node->name = NULL;
  node->parent = NULL;
  node->next = NULL;
  node->prev = NULL;
  return node;
}

//

fs_node_t *find_child(fs_node_t *node, const char *name) {
  if (!IS_IFDIR(node->mode)) {
    errno = ENOTDIR;
    return NULL;
  }

  node = node->ifdir.first;
  while (node) {
    if (strcmp(node->name, name) == 0) {
      return node;
    }
    node = node->next;
  }

  errno = ENOENT;
  return NULL;
}

int add_child(fs_node_t *parent, fs_node_t *child) {
  if (!IS_IFDIR(parent->mode)) {
    errno = ENOTDIR;
    return -1;
  }

  child->parent = parent;
  if (parent->ifdir.last == NULL) {
    parent->ifdir.first = child;
    parent->ifdir.last = child;
  } else {
    child->prev = parent->ifdir.last;
    parent->ifdir.last->next = child;
    parent->ifdir.last = child;
  }
  return 0;
}

void swap_node(fs_node_t *orig_node, fs_node_t *new_node) {
  new_node->parent = orig_node->parent;
  new_node->prev = orig_node->prev;
  new_node->next = orig_node->next;

  if (orig_node->prev) {
    orig_node->prev->next = new_node;
  }
  if (orig_node->next) {
    orig_node->next->prev = new_node;
  }

  if (orig_node->parent && orig_node->parent->ifdir.first == orig_node) {
    orig_node->parent->ifdir.first = new_node;
  }

  // break any links to this node
  char *node_path = path_from_node(orig_node);
  lock(links.rwlock);
  map_delete(&links.hash_table, node_path);
  unlock(links.rwlock);
  kfree(node_path);
}

void remove_node(fs_node_t *node) {
  // cleans up all external references to the node
  if (node->prev) {
    node->prev->next = node->next;
  }
  if (node->next) {
    node->next->prev = node->prev;
  }

  if (node->parent && node->parent->ifdir.first == node) {
    node->parent->ifdir.first = node->next;
  }

  // break any links to this node
  char *node_path = path_from_node(node);
  lock(links.rwlock);
  map_delete(&links.hash_table, node_path);
  unlock(links.rwlock);
  kfree(node_path);
}

//

inode_t *get_inode(fs_node_t *node) {
  if (!(node->mode & INODE_FILE_MASK)) {
    errno = ENOENT;
    return NULL;
  }

  inode_t *inode = __fetch_inode(inodes, node->inode);
  if (inode != NULL) {
    return inode;
  }

  fs_t *fs = node->fs;
  inode = fs->impl->locate(fs, node->inode);
  if (inode == NULL) {
    errno = ENOENT;
  }
  return inode;
}

inode_t *create_inode(fs_node_t *parent, mode_t mode) {
  fs_t *fs = parent->fs;
  inode_t *inode = fs->impl->create(fs, mode);
  if (inode) {
    inode->impl = parent->fs->impl;
    inode->mode = mode;
    __cache_inode(inodes, inode->ino, inode);
  }
  return inode;
}

//

int validate_open_flags(fs_node_t *node, int flags) {
  // only one of the file access modes can be set
  if (__popcnt64((uint64_t) (flags & ACCESS_MODE_MASK)) > 1) {
    errno = EINVAL;
    return -1;
  }

  if (node) {
    if (
      (flags & O_EXEC && IS_IFDIR(node->mode)) ||
      (flags & O_RDWR && IS_IFIFO(node->mode)) ||
      (flags & O_SEARCH && !IS_IFDIR(node->mode))
    ) {
      errno = EINVAL;
      return -1;
    }

    if (flags & O_DIRECTORY && !IS_IFDIR(node->mode)) {
      errno = ENOTDIR;
      return -1;
    }
  } else if (
    (flags & O_CREAT && flags & O_DIRECTORY) &&
    !((flags & O_WRONLY || flags & O_RDWR))
  ) {
    errno = EINVAL;
    return -1;
  }

  return 0;
}

//
// Filesystem API
//

void fs_init() {
  kprintf("[fs] initializing...\n");

  // create the filesystem root
  root = kmalloc(sizeof(fs_node_t));
  root->inode = 0;
  root->dev = -1;
  root->mode = S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH;
  root->name = "/";
  root->parent = NULL;
  root->next = root;

  // inodes
  inodes = __create_inode_table();

  // links
  links.hash_table.map.hasher = hash;
  links.hash_table.map.capacity = 1024;
  links.hash_table.map.load_factor = LOAD_FACTOR;
  map_init(&links.hash_table);
  spinrw_init(&links.rwlock);

  // cache
  cache.hash_table.map.hasher = hash;
  cache.hash_table.map.capacity = 1024;
  cache.hash_table.map.load_factor = LOAD_FACTOR;
  map_init(&cache.hash_table);
  spinrw_init(&cache.rwlock);

  fs_register_device_driver(ahci_driver);
  fs_discover_devices();

  fs_t *fs = ramfs_mount(-1, root);
  add_child(root, fs->root);

  FILES = __create_file_table();

  kprintf("[fs] done!\n");
}

//

int fs_mount(fs_driver_t *driver, fs_node_t *device, const char *path) {
  char *dirname = path_dirname(path);
  fs_node_t *parent = path_get_node(root, dirname, 0);
  kfree(dirname);
  if (parent == NULL) {
    kfree(dirname);
    return -1;
  }

  if (!IS_IFBLK(device->mode)) {
    errno = ENOTBLK;
    return -1;
  }

  char *basename = path_basename(path);
  fs_node_t *mount = kmalloc(sizeof(fs_node_t));
  mount->name = basename;
  fs_node_t *existing = find_child(parent, basename);
  if (existing != NULL) {
    // swap the existing node and save it
    swap_node(existing, mount);
    mount->ifmnt.shadow = existing;
  } else {
    add_child(parent, mount);
    mount->ifmnt.shadow = NULL;
  }

  fs_device_t *dev = device->ifblk.device;
  mount->inode = 0;
  mount->dev = dev->id;
  mount->name = basename;
  mount->mode = S_IFMNT | S_IFDIR;

  fs_t *instance = driver->impl->mount(dev->id, mount);
  if (instance == NULL) {
    // free allocated resources
    if (existing != NULL) {
      // swap the node back
      swap_node(mount, mount->ifmnt.shadow);
    } else {
      remove_node(mount);
    }
    kfree(basename);
    kfree(mount);
    return -1;
  }

  mount->dev = dev->id;
  return 0;
}

int fs_unmount(const char *path) {
  fs_node_t *mount = path_get_node(root, path, 0);
  if (mount == NULL) {
    return -1;
  }

  if (!IS_IFMNT(mount->mode)) {
    errno = ENOTMNT;
    return -1;
  }

  fs_t *instance = mount->fs;

  // sync all data before unmounting
  if (instance->driver->impl->sync(instance) == -1) {
    return -1;
  }
  if (instance->impl->unmount(instance, mount) == -1) {
    return -1;
  }

  if (mount->ifmnt.shadow == NULL) {
    remove_node(mount);
  } else {
    swap_node(mount, mount->ifmnt.shadow);
  }

  kfree(instance);
  kfree(mount);
  return 0;
}

int fs_create(fs_node_t *parent, const char *name, mode_t mode) {

}

int fs_remove(fs_node_t *parent, const char *name) {

}

//
// Filesystem Syscalls
//

int fs_open(const char *filename, int flags, mode_t mode) {
  fs_node_t *node = path_get_node(root, filename, flags);
  if (!node && errno != ENOENT) {
    return -1;
  }

  if (validate_open_flags(node, flags) < 0) {
    return -1;
  } else if (node == NULL && !(flags & O_CREAT)){
    errno = ENOENT;
    return -1;
  } else if (node && IS_IFSOCK(node->mode)) {
    errno = EOPNOTSUPP;
    return -1;
  }

  if (node == NULL) {
    // create the node first
    char *dirname = path_dirname(filename);
    fs_node_t *parent = path_get_node(root, dirname, flags);
    kfree(dirname);
    if (!parent) {
      return -1;
    }

    mode |= (flags & O_DIRECTORY) ?
      S_IFDIR : S_IFREG;

    inode_t *inode = create_inode(parent, mode);
    if (inode == NULL) {
      return -1;
    }

    char *basename = path_basename(filename);
    node = __create_fs_node();
    node->inode = inode->ino;
    node->dev = inode->dev;
    node->mode = inode->mode;
    node->name = basename;
    node->fs = parent->fs;

    add_child(parent, node);
  }

  inode_t *inode = get_inode(node);
  if (inode == NULL) {
    return -1;
  }

  file_table_t *files = FILES;
  file_t *file = __create_file();

  lock(files->lock);
  int fd = files->next_fd;
  files->next_fd++;

  file->fd = fd;
  file->flags = flags;
  file->node = node;
  if (flags & O_APPEND) {
    file->offset = inode->size;
  } else {
    file->offset = 0;
  }

  rb_tree_insert(files->files, fd, file);
  unlock(files->lock);
  return fd;
}

int fs_close(int fd) {
  file_table_t *files = FILES;

  lock(files->lock);
  rb_node_t *node = rb_tree_find(files->files, fd);
  if (node == NULL) {
    unlock(files->lock);
    errno = EBADF;
    return -1;
  }
  file_t *file = node->data;
  rb_tree_delete_node(files->files, node);
  unlock(files->lock);

  kfree(file);
  return 0;
}

//

ssize_t fs_read(int fd, void *buf, size_t nbytes) {
  file_table_t *files = FILES;

  lock(files->lock);
  rb_node_t *node = rb_tree_find(files->files, fd);
  if (node == NULL) {
    unlock(files->lock);
    errno = EBADF;
    return -1;
  }
  file_t *file = node->data;
  unlock(files->lock);

  //

  inode_t *inode = get_inode(file->node);
  if (inode == NULL) {
    return -1;
  }

  aquire(file->lock);
  lock(inode->lock);
  ssize_t nread = inode->impl->read(file->node->fs, inode, file->offset, nbytes, buf);
  unlock(inode->lock);
  release(file->lock);

  return nread;
}

ssize_t fs_write(int fd, void *buf, size_t nbytes) {
  file_table_t *files = FILES;

  lock(files->lock);
  rb_node_t *node = rb_tree_find(files->files, fd);
  if (node == NULL) {
    unlock(files->lock);
    errno = EBADF;
    return -1;
  }
  file_t *file = node->data;
  unlock(files->lock);

  //

  inode_t *inode = get_inode(file->node);
  if (inode == NULL) {
    return -1;
  }

  aquire(file->lock);
  lock(inode->lock);
  ssize_t nwritten = inode->impl->write(file->node->fs, inode, file->offset, nbytes, buf);
  unlock(inode->lock);
  release(file->lock);

  return nwritten;
}

off_t fs_lseek(int fd, off_t offset, int whence) {
  file_table_t *files = FILES;

  lock(files->lock);
  rb_node_t *node = rb_tree_find(files->files, fd);
  if (node == NULL) {
    unlock(files->lock);
    errno = EBADF;
    return -1;
  }
  file_t *file = node->data;
  unlock(files->lock);

  if (IS_IFIFO(file->node->mode)) {
    errno = ESPIPE;
    return -1;
  }

  inode_t *inode = get_inode(file->node);
  if (inode == NULL) {
    return -1;
  }

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

}

int fs_unlink(const char *path) {

}

int fs_symlink(const char *path1, const char *path2) {

}

int fs_rename(const char *oldfile, const char *newfile) {

}

int fs_chmod(const char *path, mode_t mode) {

}

int fs_chown(const char *path, uid_t owner, gid_t group) {

}

//

int fs_opendir(const char *filename) {

}

int fs_closedir(int fd) {

}

int fs_mkdir(const char *path, mode_t mode) {

}

int fs_chdir(const char *path) {

}

//

dirent_t *fs_readdir(int fd) {

}

long fs_telldir(int fd) {

}

void fs_seekdir(int fd, long loc) {

}




