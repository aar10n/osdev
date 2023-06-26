//
// Created by Aaron Gill-Braun on 2022-06-17.
//

#ifndef KERNEL_MM_PMALLOC_H
#define KERNEL_MM_PMALLOC_H

#include <kernel/base.h>
#include <kernel/mm_types.h>

struct frame_allocator_impl;
struct bitmap;

typedef enum zone_type {
  ZONE_TYPE_LOW,
  ZONE_TYPE_DMA,
  ZONE_TYPE_NORMAL,
  ZONE_TYPE_HIGH,
  MAX_ZONE_TYPE
} zone_type_t;

// zone boundaries
#define ZONE_LOW_MAX    SIZE_1MB
#define ZONE_DMA_MAX    SIZE_16MB
#define ZONE_NORMAL_MAX SIZE_4GB
#define ZONE_HIGH_MAX   UINT64_MAX

/**
 * frame_allocator represents a generic frame allocator.
 * this struct wraps the frame_allocator_impl interface to provide a
 * simple page allocation api.
 */
typedef struct frame_allocator {
  uintptr_t base;
  size_t size;
  size_t free;
  spinlock_t lock;

  struct frame_allocator_impl *impl;
  void *data;

  LIST_ENTRY(struct frame_allocator) list;
} frame_allocator_t;

struct frame_allocator_impl {
  void *(*fa_init)(frame_allocator_t *fa);
  intptr_t (*fa_alloc)(frame_allocator_t *fa, size_t count, size_t pagesize);
  int (*fa_reserve)(frame_allocator_t *fa, uintptr_t frame, size_t count, size_t pagesize);
  void (*fa_free)(frame_allocator_t *fa, uintptr_t frame, size_t count, size_t pagesize);
};

void init_mem_zones();
int reserve_pages(uintptr_t address, size_t count, size_t pagesize);

// physical memory api
//

page_t *alloc_pages_zone(zone_type_t zone_type, size_t count, size_t pagesize);
page_t *alloc_pages_size(size_t count, size_t pagesize);
page_t *alloc_pages(size_t count);
page_t *alloc_pages_at(uintptr_t address, size_t count, size_t pagesize);
void free_pages(page_t *pages);
page_t *alloc_cow_page(page_t *page);
page_t *alloc_cow_pages_at(uintptr_t address, size_t count, size_t pagesize);

page_t *page_list_remove_head(page_t **list);
page_t *page_list_add_tail(page_t *head, page_t *tail, page_t *page);
bool mm_is_kernel_code_ptr(uintptr_t ptr);
bool mm_is_kernel_data_ptr(uintptr_t ptr);

#endif
