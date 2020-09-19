//
// Created by Aaron Gill-Braun on 2020-09-14.
//

#include <math.h>
#include <stdio.h>

#include <kernel/mem/cache.h>
#include <kernel/mem/heap.h>

void create_cache(cache_t *cache) {
  kprintf("creating cache \"%s\"\n", cache->name);

  size_t size = cache->size;
  size_t count = cache->count;

  size_t total_size = size * count / PAGE_SIZE;
  size_t real_count = 0;
  int order = log2(total_size);
  while (order >= 0) {
    int next_order;
    if (order > MAX_ORDER) {
      next_order = MAX_ORDER;
      order -= MAX_ORDER;
    } else {
      next_order = order;
      order = -1;
    }

    page_t *page = alloc_pages(next_order, 0);
    uintptr_t ptr = page->virt_addr;
    for (size_t i = 0; i < 1 << next_order; i++) {
      slab_t *slab = kmalloc(sizeof(slab_t));
      slab->ptr = (void *) ptr;
      slab->next = NULL;
      if (cache->first == NULL) {
        cache->first = slab;
        cache->last = slab;
      } else {
        cache->last->next = slab;
        cache->last = slab;
      }

      real_count++;
      ptr += size;
    }
  }

  kprintf("cache created!\n");
}

void *cache_alloc(cache_t *cache) {
  if (cache->first == NULL) {
    return NULL;
  }

  slab_t *slab = cache->first;
  if (slab == cache->last || slab->next == NULL) {
    cache->first = NULL;
    cache->last = NULL;
  } else {
    cache->first = slab->next;
  }

  void *ptr = slab->ptr;
  kfree(slab);
  return ptr;
}

void cache_free(cache_t *cache, void *ptr) {
  slab_t *slab = kmalloc(sizeof(slab_t));
  slab->ptr = ptr;
  slab->next = NULL;

  if (cache->first == NULL) {
    cache->first = slab;
    cache->last = slab;
  } else {
    cache->last->next = slab;
    cache->last = slab;
  }
}
