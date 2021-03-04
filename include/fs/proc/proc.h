//
// Created by Aaron Gill-Braun on 2020-11-02.
//

#ifndef FS_PROC_PROC_H
#define FS_PROC_PROC_H

#include <base.h>
#include <fs.h>
#include <ramfs/ramfs.h>

extern fs_impl_t procfs_impl;
extern fs_driver_t procfs_driver;

dirent_t *procfs_link(fs_t *fs, inode_t *inode, inode_t *parent, char *name);
int procfs_unlink(fs_t *fs, inode_t *inode, dirent_t *dirent);

#endif
