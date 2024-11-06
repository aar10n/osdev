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

void init_mem_zones();
int reserve_pages(enum pg_rsrv_kind kind, uintptr_t address, size_t count, size_t pagesize);

// page allocation api

__ref page_t *alloc_pages_zone(zone_type_t zone_type, size_t count, size_t pagesize);
__ref page_t *alloc_pages_size(size_t count, size_t pagesize);
__ref page_t *alloc_pages(size_t count);
__ref page_t *alloc_pages_at(uintptr_t address, size_t count, size_t pagesize);
__ref page_t *alloc_nonowned_pages_at(uintptr_t address, size_t count, size_t pagesize);
__ref page_t *alloc_cow_pages(page_t *pages);
__ref page_t *alloc_shared_pages(page_t *pages);
void drop_pages(__move page_t **pagesref);

// page struct api

struct pte *pte_struct_alloc(page_t *page, uint64_t *entry, vm_mapping_t *vm);
void pte_struct_free(struct pte **pteptr);

void page_add_mapping(page_t *page, struct pte *pte);
struct pte *page_remove_mapping(page_t *page, vm_mapping_t *vm);
struct pte *page_get_mapping(page_t *page, vm_mapping_t *vm);
void page_update_flags(page_t *page, uint32_t flags);

__ref page_t *page_list_join(__ref page_t *head, __ref page_t *tail);
__ref page_t *page_list_split(__ref page_t *pages, size_t count, __out page_t **tailref);

bool is_kernel_code_ptr(uintptr_t ptr);
bool is_kernel_data_ptr(uintptr_t ptr);

#endif
