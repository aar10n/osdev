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


typedef struct ramfs_super {
  bitmap_t inodes; // inode number bitmap
  spinlock_t lock; // bitmap spinlock
} ramfs_super_t;

void ramfs_init();
super_block_t *ramfs_mount(file_system_t *fs, blkdev_t *dev, dentry_t *mount);

#endif
