//
// Created by Aaron Gill-Braun on 2024-10-24.
//

#ifndef KERNEL_MM_PGCACHE_H
#define KERNEL_MM_PGCACHE_H

#include <kernel/mm_types.h>

#include <kernel/ref.h>

#define PGCACHE_MAX_ORDER   8
#define PGCACHE_FANOUT      16 // TODO: make fanout adaptive

// Total Memory = Fanout^(Order+1) * PAGE_SIZE
//
// Fanout=16  PAGE_SIZE=4KB
// Order | Total Memory
// ------|--------------
// 0     | 64KB
// 1     | 1MB
// 2     | 16MB
// 3     | 256MB
// 4     | 4GB
//
// Fanout=16  PAGE_SIZE=2MB
// Order | Total Memory
// ------|--------------
// 0     | 128MB
// 1     | 2GB
// 2     | 32GB
// 3     | 512GB
// 4     | 8TB
//

struct pgcache_node;

struct pgcache {
  uint16_t order;         // order (depth) of the cache tree
  uint16_t bits_per_lvl;  // bits of key used per level to index
  uint32_t pg_size;       // size of each page
  size_t max_capacity;    // the maximum cachable memory capacity
  size_t count;           // number of pages in the cache
  _refcount;              // reference count

  struct pgcache_node *root;
  LIST_HEAD(struct pgcache_node) leaf_nodes;
};


__ref struct pgcache *pgcache_alloc(uint16_t order, uint32_t pg_size);
__ref struct pgcache *pgcache_clone(struct pgcache *cache);
void pgcache_free(__move struct pgcache **cacheptr);
void pgcache_resize(struct pgcache *cache, uint16_t new_order);
__ref page_t *pgcache_lookup(struct pgcache *cache, size_t off);
void pgcache_insert(struct pgcache *cache, size_t off, __ref page_t *page, __move page_t **out_old);
void pgcache_remove(struct pgcache *cache, size_t off, __move page_t **out_page);

typedef void (*pgcache_visit_t)(page_t **pagesref, size_t off, void *data);
void pgcache_visit_pages(struct pgcache *cache, size_t start_off, size_t end_off, pgcache_visit_t fn, void *data);

static inline size_t pgcache_size_to_order(size_t total_size, size_t pg_size) {
  size_t order = 0;
  size_t size = pg_size;
  while (size < total_size) {
    size *= PGCACHE_FANOUT;
    order++;
  }
  return order;
}

//

#endif
