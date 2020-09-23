//
// Created by Aaron Gill-Braun on 2020-09-14.
//

#ifndef KERNEL_MEM_CACHE_H
#define KERNEL_MEM_CACHE_H

#include <stddef.h>

typedef struct slab {
  void *ptr;
  struct slab *next;
} slab_t;

typedef struct {
  const char *name;
  slab_t *first;
  slab_t *last;
  size_t size;
  size_t count;
} cache_t;

void create_cache(cache_t *cache);
void *cache_alloc(cache_t *cache);
void cache_free(cache_t *cache, void *ptr);

#endif
