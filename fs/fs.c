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

// #define FILES (current->files)
#define FILES (PERCPU->files)

//
// Filesystem API
//

void fs_init() {
  kprintf("[fs] initializing...\n");

  // create the filesystem root
  fs_root = kmalloc(sizeof(fs_node_t));
  fs_root->inode = 0;
  fs_root->dev = -1;
  fs_root->mode = S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH;
  fs_root->name = "/";
  fs_root->parent = NULL;
  fs_root->next = fs_root;

  path_init();
  vfs_init();

  fs_register_device(NULL, &pseudo_impl);

  PERCPU->files = create_file_table();
  PERCPU->pwd = fs_root;

  kprintf("[fs] done!\n");
}

//

int fs_mount(fs_driver_t *driver, const char *device, const char *path) {
  fs_node_t *dev_node = vfs_get_node(str_to_path(device), 0);
  if (dev_node == NULL) {
    return -1;
  } else if (!IS_IFBLK(dev_node->mode)) {
    errno = ENOTBLK;
    return -1;
  } else if (strcmp(path, "/") == 0) {
    errno = EACCES;
    return -1;
  }

  path_t p = str_to_path(path);
  fs_node_t *parent = vfs_get_node(path_dirname(p), O_DIRECTORY);
  if (parent == NULL) {
    return -1;
  }

  fs_device_t *dev = dev_node->ifblk.device;
  fs_node_t *mount = vfs_create_node();
  mount->dev = dev->id;

  path_t basename = path_basename(p);
  mount->name = path_to_str(basename);
  mount->mode = S_IFMNT;
  mount->ifmnt.shadow = NULL;

  fs_node_t *child = vfs_find_child(parent, basename);
  if (child == NULL) {
    vfs_add_node(parent, mount);
  } else {
    vfs_swap_node(child, mount);
  }

  fs_t *instance = driver->impl->mount(mount->dev, mount);
  if (instance == NULL) {
    // free allocated resources
    kfree(mount);
    return -1;
  }

  return 0;
}

int fs_unmount(const char *path) {
  fs_node_t *mount = vfs_get_node(str_to_path(path), 0);
  if (mount == NULL) {
    return -1;
  } else if (!IS_IFMNT(mount->mode)) {
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

  if (mount->ifmnt.shadow == NULL) {
    vfs_remove_node(mount);
  } else {
    vfs_swap_node(mount, mount->ifmnt.shadow);
  }

  kfree(instance);
  kfree(mount);
  return 0;
}

//
// Filesystem Syscalls
//

int fs_open(const char *filename, int flags, mode_t mode) {
  // fs_node_t *node = path_get_node(fs_root, filename, flags);
  fs_node_t *node = vfs_get_node(str_to_path(filename), flags);
  if (node == NULL && (errno != ENOENT && flags & O_CREAT)) {
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
    fs_node_t *parent = vfs_get_node(dirname, 0);
    if (parent == NULL) {
      return -1;
    }

    mode |= (flags & O_DIRECTORY) ? S_IFDIR : S_IFREG;

    inode = inode_create(parent->fs, mode);
    if (inode == NULL) {
      return -1;
    }

    path_t basename = path_basename(path);
    node = vfs_create_node_from_inode(inode);
    node->name = path_to_str(basename);
    node->fs = parent->fs;

    vfs_add_node(parent, node);
  } else {
    inode = inode_get(node);
    if (inode == NULL) {
      return -1;
    }
  }

  if (file_create(node, flags) == NULL) {
    return -1;
  }
  return 0;
}

int fs_close(int fd) {
  file_t *file = file_get(fd);
  if (file == NULL) {
    return -1;
  }
  file_delete(file);
  return 0;
}

//

ssize_t fs_read(int fd, void *buf, size_t nbytes) {
  file_t *file = file_get(fd);
  if (file == NULL) {
    return -1;
  }

  inode_t *inode = inode_get(file->node);
  if (inode == NULL) {
    return -1;
  }

  if (file->flags & O_APPEND) {
    file->offset = inode->size;
  }

  fs_t *fs = file->node->fs;
  aquire(file->lock);
  lock(inode->lock);
  ssize_t nread = fs->impl->read(file->node->fs, inode, file->offset, nbytes, buf);
  unlock(inode->lock);
  release(file->lock);

  return nread;
}

ssize_t fs_write(int fd, void *buf, size_t nbytes) {
  file_t *file = file_get(fd);
  if (file == NULL) {
    return -1;
  }

  inode_t *inode = inode_get(file->node);
  if (inode == NULL) {
    return -1;
  }

  fs_t *fs = file->node->fs;
  aquire(file->lock);
  lock(inode->lock);
  ssize_t nwritten = fs->impl->write(file->node->fs, inode, file->offset, nbytes, buf);
  unlock(inode->lock);
  release(file->lock);

  return nwritten;
}

off_t fs_lseek(int fd, off_t offset, int whence) {
  file_t *file = file_get(fd);
  if (file == NULL) {
    return -1;
  }

  if (IS_IFIFO(file->node->mode)) {
    errno = ESPIPE;
    return -1;
  }

  inode_t *inode = inode_get(file->node);
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




