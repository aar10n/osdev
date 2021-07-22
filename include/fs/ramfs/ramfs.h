//
// Created by Aaron Gill-Braun on 2020-11-02.
//

#ifndef FS_RAMFS_RAMFS_H
#define FS_RAMFS_RAMFS_H

#include <fs.h>
#include <bitmap.h>

#define RAMFS_MAX_FILES 1024


typedef struct ramfs_super {
  bitmap_t inodes; // inode number bitmap
  spinlock_t lock; // bitmap spinlock
} ramfs_super_t;

void ramfs_init();

#endif
