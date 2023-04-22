//
// Created by Aaron Gill-Braun on 2020-10-31.
//

#ifndef FS_INODE_H
#define FS_INODE_H

#include <fs_types.h>

// ============ Virtual API ============

inode_t *i_alloc_empty();
void i_free(inode_t *inode);
int i_link_dentry(inode_t *inode, dentry_t *dentry);
int i_unlink_dentry(inode_t *inode, dentry_t *dentry);

// ============= Operations =============

dentry_t *i_locate(inode_t *inode, dentry_t *dentry, const char *name, size_t name_len);
int i_loaddir(inode_t *inode, dentry_t *dentry);
int i_create(inode_t *dir, inode_t *inode, dentry_t *dentry);
int i_mknod(inode_t *dir, dentry_t *dentry, dev_t dev);
int i_link(inode_t *dir, inode_t *inode, dentry_t *dentry);
int i_unlink(inode_t *dir, dentry_t *dentry);
int i_symlink(inode_t *dir, inode_t *dentry, const char *path);
int i_readlink(dentry_t *dentry, size_t buflen, char *buffer);
int i_mkdir(inode_t *dir, dentry_t *dentry);
int i_rmdir(inode_t *dir, dentry_t *dentry);
int i_rename(inode_t *o_dir, dentry_t *o_dentry, inode_t *n_dir, dentry_t *n_dentry);

#endif
