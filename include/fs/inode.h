//
// Created by Aaron Gill-Braun on 2020-10-31.
//

#ifndef FS_INODE_H
#define FS_INODE_H

#include <fs.h>

inode_t *i_alloc(ino_t ino, super_block_t *sb);

int i_create(inode_t *dir, dentry_t *dentry, mode_t mode);
dentry_t *i_lookup(inode_t *dir, const char *name);
int i_link(inode_t *dir, dentry_t *dentry, dentry_t *new_dentry);
int i_unlink(inode_t *dir, dentry_t *dentry);
int i_symlink(inode_t *dir, dentry_t *dentry, const char *path);
int i_mkdir(inode_t *dir, dentry_t *dentry, mode_t mode);
int i_rmdir(inode_t *dir, dentry_t *dentry);
int i_mknod(inode_t *dir, dentry_t *dentry, mode_t mode, dev_t dev);
int i_rename(inode_t *old_dir, dentry_t *old_dentry, inode_t *new_dir, dentry_t *new_dentry);
int i_readlink(dentry_t *dentry, char *buffer, int buflen);
void i_truncate(inode_t *inode);

#endif
