//
// Created by Aaron Gill-Braun on 2021-07-14.
//

#ifndef FS_DCACHE_H
#define FS_DCACHE_H

#include <fs_types.h>

struct page;

typedef struct dcache_entry {
  struct dentry *dentry;
  struct dcache_entry *next;
} dcache_entry_t;

typedef struct dcache {
  uint32_t size;
  uint32_t count;
  spinlock_t lock;
  struct dentry **buckets;
  struct page *pages;
  // TODO: use a tree (prefix trie?) to track hierarchy of paths to make
  //       invalidation practical. for now we
} dcache_t;

struct dcache *dcache_create();
void dcache_destroy(struct dcache *dcache);
int dcache_put(struct dcache *dcache, struct dentry *dentry);
int dcache_remove(struct dcache *dcache, struct dentry *dentry);


#endif
