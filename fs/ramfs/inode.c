//
// Created by Aaron Gill-Braun on 2023-05-14.
//

#include <ramfs/ramfs.h>

#include <kio.h>
#include <panic.h>


int ramfs_i_loaddir(inode_t *inode, dentry_t *dentry) {
  return 0;
}

int ramfs_i_create(inode_t *inode, dentry_t *dentry, inode_t *dir) {
  ramfs_file_t *file = ramfs_alloc_file_type(RAMFS_MEM_PAGE, inode->size);
  inode->data = file;
  return 0;
}

int ramfs_i_mknod(inode_t *inode, dentry_t *dentry, inode_t *dir, dev_t dev) {
  return 0;
}

int ramfs_i_symlink(inode_t *inode, dentry_t *dentry, inode_t *dir, const char *path, size_t len) {
  ramfs_file_t *file = ramfs_alloc_file(len);

  kio_t kio = kio_new_readonly(path, len);
  if (ramfs_write_file(file, 0, &kio) != len) {
    return -EIO;
  }
  return 0;
}

int ramfs_i_readlink(inode_t *inode, size_t buflen, char *buffer) {
  ramfs_file_t *file = inode->data;

  kio_t kio = kio_new_writeonly(buffer, buflen);
  if (ramfs_read_file(file, 0, &kio) != buflen) {
    return -ENOBUFS;
  }
  return 0;
}

int ramfs_i_unlink(inode_t *inode, dentry_t *dentry, inode_t *dir) {
  ramfs_file_t *file = inode->data;
  ramfs_free_file(file);
  return 0;
}

int ramfs_i_mkdir(inode_t *inode, dentry_t *dentry, inode_t *dir) {
  return 0;
}

int ramfs_i_rmdir(inode_t *inode, dentry_t *dentry, inode_t *dir) {
  return 0;
}

int ramfs_i_rename(inode_t *inode, dentry_t *o_dentry, inode_t *o_dir, dentry_t *n_dentry, inode_t *n_dir) {
  return 0;
}
