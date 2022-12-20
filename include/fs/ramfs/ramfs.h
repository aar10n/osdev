//
// Created by Aaron Gill-Braun on 2020-11-02.
//

#ifndef FS_RAMFS_RAMFS_H
#define FS_RAMFS_RAMFS_H

#include <fs.h>
#include <bitmap.h>

#define RAMFS_MAX_FILES 1024

extern super_block_ops_t *ramfs_super_ops;
extern inode_ops_t *ramfs_inode_ops;
extern dentry_ops_t *ramfs_dentry_ops;
extern file_ops_t *ramfs_file_ops;

// Super Ops
inode_t *ramfs_alloc_inode(super_block_t *sb);
int ramfs_destroy_inode(super_block_t *sb, inode_t *inode);
int ramfs_read_inode(super_block_t *sb, inode_t *inode);
int ramfs_write_inode(super_block_t *sb, inode_t *inode);
// Inode Ops
int ramfs_create(inode_t *dir, dentry_t *dentry, mode_t mode);
int ramfs_mknod(inode_t *dir, dentry_t *dentry, mode_t mode, dev_t dev);
int ramfs_mkdir(inode_t *dir, dentry_t *dentry, mode_t mode);
int ramfs_rename(inode_t *old_dir, dentry_t *old_dentry, inode_t *new_dir, dentry_t *new_dentry);
// File Ops
int ramfs_open(file_t *file, dentry_t *dentry);
int ramfs_flush(file_t *file);
ssize_t ramfs_read(file_t *file, char *buf, size_t count, off_t *offset);
ssize_t ramfs_write(file_t *file, const char *buf, size_t count, off_t *offset);

typedef struct ramfs_super {
  bitmap_t inodes; // inode number bitmap
  spinlock_t lock; // bitmap spinlock
} ramfs_super_t;

void ramfs_init();
super_block_t *ramfs_mount(file_system_t *fs, dev_t devid, blkdev_t *dev, dentry_t *mount);

#endif
