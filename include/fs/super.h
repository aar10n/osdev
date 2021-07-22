//
// Created by Aaron Gill-Braun on 2021-07-16.
//

#ifndef FS_SUPER_H
#define FS_SUPER_H

#include <fs.h>

inode_t *sb_alloc_inode(super_block_t *sb);
int sb_destroy_inode(inode_t *inode);
int sb_read_inode(dentry_t *dentry);
int sb_write_inode(inode_t *inode);

#endif
