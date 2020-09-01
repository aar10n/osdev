//
// Created by Aaron Gill-Braun on 2020-08-30.
//

#ifndef KERNEL_MEM_CACHE_H
#define KERNEL_MEM_CACHE_H

#include <stddef.h>
#include <stdint.h>

#include <kernel/mem/mm.h>

typedef struct cache {
  const char *name;
  size_t size;
  size_t align;
  uint32_t count;
  uint32_t capacity;
  void *items[];
} cache_t;

cache_t *cache_create(
    char *name,
    size_t size,
    size_t align,
    page_t source
);
void cache_destroy(cache_t *cache);

void *cache_pop(cache_t *cache);
void cache_push(cache_t *cache, void *item);



#endif
