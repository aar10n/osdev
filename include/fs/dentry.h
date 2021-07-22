//
// Created by Aaron Gill-Braun on 2021-07-17.
//

#ifndef FS_DENTRY_H
#define FS_DENTRY_H

#include <fs.h>

dentry_t *d_alloc(dentry_t *parent, const char *name);
void d_attach(dentry_t *dentry, inode_t *inode);
void d_detach(dentry_t *dentry);
void d_sync(dentry_t *dentry);
int d_populate_dir(dentry_t *dir);
void d_add_child(dentry_t *parent, dentry_t *child);
void d_remove_child(dentry_t *parent, dentry_t *child);
void d_destroy(dentry_t *dentry);

#endif
