//
// Created by Aaron Gill-Braun on 2023-03-21.
//

#ifndef FS_DENTRY_H
#define FS_DENTRY_H

#include <fs_types.h>
#include <path.h>

bool d_hash_equal(hash_t a, hash_t b);
uint64_t d_hash_index(hash_t hash, size_t size);
hash_t d_default_hash_name(const char *name, size_t len);

// ============ Virtual API ============

dentry_t *d_alloc_empty();
dentry_t *d_alloc(const char *name, mode_t mode, const struct dentry_ops *ops);
dentry_t *d_alloc_dir(const char *name, const struct dentry_ops *ops);
dentry_t *d_alloc_dot(const struct dentry_ops *ops);
dentry_t *d_alloc_dotdot(const struct dentry_ops *ops);
void d_free(dentry_t *dentry);
int d_add_child(dentry_t *parent, dentry_t *child);
int d_remove_child(dentry_t *parent, dentry_t *child);
dentry_t *d_lookup_child(dentry_t *parent, const char *name, size_t len);

// ============= Operations =============

hash_t d_hash_str(const struct dentry_ops *ops, const char *name, size_t len);
hash_t d_hash_path(const struct dentry_ops *ops, path_t path);
int d_compare(const struct dentry *d, const char *name, size_t len);
int d_compare_path(const struct dentry *d, path_t path);

#endif
