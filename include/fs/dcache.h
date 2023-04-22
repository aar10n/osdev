//
// Created by Aaron Gill-Braun on 2021-07-14.
//

#ifndef FS_DCACHE_H
#define FS_DCACHE_H

#include <fs_types.h>
#include <path.h>
#include <sbuf.h>
#include <queue.h>

// these entries are used to track
typedef struct dcache_dir {
  size_t count;   // number of hashes
  size_t size;    // size of hashes array
  hash_t *hashes; // dhashes of subdirectories
  dentry_t *dentry;
  LIST_ENTRY(struct dcache_dir) list;
} dcache_dir_t;

typedef struct dcache {
  const struct dentry *root; // root dentry of paths in this cache
  size_t size;
  size_t count;
  mutex_t lock;
  struct dentry **buckets;  // path hash -> dentry
  struct dcache_dir **dirs; // path hash -> dcache_dir
  struct page *pages;
} dcache_t;

#define DCACHE_LOCK(dcache) __type_checked(dcache_t*, dcache, mutex_lock(&(dcache)->lock))
#define DCACHE_UNLOCK(dcache) __type_checked(dcache_t*, dcache, mutex_unlock(&(dcache)->lock))


// ============= Dcache API =============

dcache_t *dcache_create(const struct dentry *root);
void dcache_destroy(dcache_t *dcache);
int dcache_get(dcache_t *dcache, path_t path, dentry_t **dentry);
int dcache_put(dcache_t *dcache, dentry_t *dentry);
int dcache_put_path(dcache_t *dcache, path_t path, dentry_t *dentry);
int dcache_remove(dcache_t *dcache, dentry_t *dentry);

// ============= Path Operations =============

/**
 * Writes the absolute path of a dentry into a buffer.
 * The path will NOT be be null terminated.
 * @param root The root dentry.
 * @param dentry The dentry to get the path for.
 * @param buffer The buffer to write the path to.
 * @param [out] depth the depth of the dentry from the root
 * @return The number of characters in the path, or when \< 0 an error code.
 */
int get_dentry_path(const dentry_t *root, const dentry_t *dentry, sbuf_t *buf, int *depth);

/**
 * Expands a path into an absolute path and writes it to a buffer.
 * The path will NOT be null terminated.
 * @param root The root dentry.
 * @param at The dentry from which to resolve relative references.
 * @param path The path to expand.
 * @param buffer The buffer to write the path to.
 * @return The number of characters in the expanded path, or \< 0 on error.
 */
int expand_path(const dentry_t *root, const dentry_t *at, path_t path, sbuf_t *buf);

/**
 * Resolve a path to a dentry.
 *
 * @param root The root dentry
 * @param at The dentry from which to resolve relative references.
 * @param path The path to walk.
 * @param flags The flags to use for the walk.
 * @param [out] result The resolved dentry
 * @return 0 on success, \< 0 on error.
 */
int resolve_path(dentry_t *root, dentry_t *at, path_t path, int flags, dentry_t **result);

#endif
