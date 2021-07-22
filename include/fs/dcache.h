//
// Created by Aaron Gill-Braun on 2021-07-14.
//

#ifndef FS_DCACHE_H
#define FS_DCACHE_H

#include <fs.h>
#include <mm.h>

typedef struct dentry_cache {
  dentry_t **buckets;    // table buckets
  size_t nbuckets;       // number of buckets
  uint32_t count;        // number of entries
  page_t *pages;         // table pages
  spinlock_t lock;       // table spinlock
} dentry_cache_t;


void dcache_init();
dentry_t *dcache_get(const char *name, uint32_t base_hash);
void dcache_add(const char *name, dentry_t *dentry);
dentry_t *dcache_remove(const char *name, dentry_t *dentry);

int expand_path(const char *path, dentry_t *at, char *buf, size_t len);
dentry_t *resolve_path(const char *path, dentry_t *at, int flags, dentry_t **parent);
dentry_t *perform_fast_lookup(const char *path, dentry_t *at);
dentry_t *perform_full_walk(const char *path, dentry_t *at, int flags, dentry_t **parent);


#endif
