//
// Created by Aaron Gill-Braun on 2021-07-26.
//

#ifndef FS_DEVFS_DEVFS_H
#define FS_DEVFS_DEVFS_H

#include <fs.h>

extern inode_ops_t *devfs_inode_ops;
extern file_ops_t *devfs_file_ops;

void devfs_init();

#endif
