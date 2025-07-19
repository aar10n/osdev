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

enum pg_rsrv_kind {
  PG_RSRV_ANY,     // the reserved frames can be from any source
  PG_RSRV_MANAGED, // the reserved frames must be from a managed zone
  PG_RSRV_PHYS,    // the reserved frames must be from an unmanaged region
};

/**
 * frame_allocator represents a generic frame allocator.
 * this struct wraps the frame_allocator_impl interface to provide a
 * simple page allocation api.
 */
typedef struct frame_allocator {
  uintptr_t base;
  size_t size;
  size_t free;
  mtx_t lock;

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

/*
 * A non-obtrusive container for holding a list pages.
 *
 * The page_list struct allows for lists of pages to be stored without
 * relying on the page_t struct's next pointer, allowing a single page
 * to be part of multiple lists. This is the backing container for the
 * virtual memory VM_TYPE_PAGE mappings. This container holds references
 * to pages in the list.
 */
typedef struct page_list {
  size_t count;
  size_t array_sz;
  page_t **pages;
} page_list_t;


//#define PG_DPRINTF(fmt, ...) kprintf("pmalloc: %s: " fmt " [%s:%d]\n", __func__, ##__VA_ARGS__, __FILE__, __LINE__)
 #define PG_DPRINTF(fmt, ...)

#define pg_getref(pg) ({ \
  ASSERT_IS_TYPE(page_t *, pg); \
  page_t *__pg = (page_t *)(pg); \
  if (__pg) { \
    __pg ? ref_get(&__pg->refcount) : NULL; \
    PG_DPRINTF("getref %p [%p] [%d]", __pg, __pg->address, __pg->refcount); \
  } \
  __pg; \
})
#define pg_putref(pgref) ({ \
  ASSERT_IS_TYPE(page_t **, pgref); \
  page_t *__pg = *(pgref); \
  *(pgref) = NULL; \
  if (__pg) { \
    kassert(__pg->refcount > 0); \
    if (ref_put(&__pg->refcount)) { \
      PG_DPRINTF("putref %p [0]", __pg); \
      _cleanup_pages(&__pg); \
    } else {                \
      PG_DPRINTF("putref %p [%d]", __pg, __pg->refcount); \
    } \
  } \
})


void init_mem_zones();
int reserve_pages(enum pg_rsrv_kind kind, uintptr_t address, size_t count, size_t pagesize);

// page allocation api

__ref page_t *alloc_pages_zone(zone_type_t zone_type, size_t count, size_t pagesize);
__ref page_t *alloc_pages_size(size_t count, size_t pagesize);
__ref page_t *alloc_pages(size_t count);
__ref page_t *alloc_pages_at(uintptr_t address, size_t count, size_t pagesize);
__ref page_t *alloc_nonowned_pages_at(uintptr_t address, size_t count, size_t pagesize);
__ref page_t *alloc_cow_pages(page_t *pages);
void _cleanup_pages(__move page_t **pagesref);

// page list api

page_list_t *page_list_alloc_from(__ref page_t *pages);
page_list_t *page_list_clone(page_list_t *list);
void page_list_free(page_list_t **listref);
__ref page_t *page_list_getpage(page_list_t *list, size_t index);
void page_list_putpage(page_list_t *list, size_t index, __ref page_t *page);
void page_list_join(page_list_t *head, page_list_t **tailref);
/// Splits the page list at the given index, modifying the list in place
/// and returning a new page_list containing the tail portion.
page_list_t *page_list_split(page_list_t *list, size_t index);

#define page_list_foreach(var, list) \
  size_t __i; \
  page_t *(var); \
  for (__i = 0, (var) = (list)->pages[0]; __i < (list)->count; __i++, (var) = (list)->pages[__i])


/// Joins two raw page-lists together and returns a reference to the new head.
__ref page_t *raw_page_list_join(__ref page_t *head, __ref page_t *tail);

#endif
