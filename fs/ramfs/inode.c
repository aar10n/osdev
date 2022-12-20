//
// Created by Aaron Gill-Braun on 2021-07-17.
//

#include <ramfs/ramfs.h>
#include <device.h>
#include <dentry.h>
#include <thread.h>


int ramfs_create(inode_t *dir, dentry_t *dentry, mode_t mode) {
  super_block_t *sb = dir->sb;
  inode_t *inode = sb->ops->alloc_inode(sb);
  if (inode == NULL) {
    ERRNO = ENOSPC;
    return -1;
  }

  inode->mode = (mode & I_PERM_MASK) | S_IFREG;
  inode->blksize = PAGE_SIZE;
  d_attach(dentry, inode);
  return 0;
}

int ramfs_mknod(inode_t *dir, dentry_t *dentry, mode_t mode, dev_t dev) {
  if (locate_device(dev) == NULL) {
    ERRNO = ENODEV;
    return -1;
  }

  super_block_t *sb = dir->sb;
  inode_t *inode = sb->ops->alloc_inode(sb);
  if (inode == NULL) {
    ERRNO = ENOSPC;
    return -1;
  }

  inode->mode = mode;
  inode->dev = dev;
  d_attach(dentry, inode);
  return 0;
}

int ramfs_mkdir(inode_t *dir, dentry_t *dentry, mode_t mode) {
  super_block_t *sb = dir->sb;
  inode_t *inode = sb->ops->alloc_inode(sb);
  if (inode == NULL) {
    ERRNO = ENOSPC;
    return -1;
  }

  inode->mode = (mode & I_PERM_MASK) | S_IFDIR;
  d_attach(dentry, inode);
  return 0;
}

int ramfs_rename(inode_t *old_dir, dentry_t *old_dentry, inode_t *new_dir, dentry_t *new_dentry) {
  super_block_t *sb = old_dir->sb;
  inode_t *old_inode = old_dentry->inode;
  inode_t *inode = sb->ops->alloc_inode(sb);
  if (inode == NULL) {
    ERRNO = ENOSPC;
    return -1;
  } else if (old_dir->sb != new_dir->sb) {
    ERRNO = EXDEV;
    return -1;
  }

  inode->mode = old_inode->mode;
  d_attach(new_dentry, inode);
  return 0;
}

//

// int (*delete)(dentry_t *dentry);

//

static inode_ops_t inode_ops = {
  .create = ramfs_create,
  .mknod = ramfs_mknod,
  .mkdir = ramfs_mkdir,
  .rename = ramfs_rename,
};

static dentry_ops_t dentry_ops = {};

inode_ops_t *ramfs_inode_ops = &inode_ops;
dentry_ops_t *ramfs_dentry_ops = &dentry_ops;
