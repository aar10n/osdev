//
// Created by Aaron Gill-Braun on 2021-03-24.
//

#ifndef INCLUDE_KERNEL_MM_H
#define INCLUDE_KERNEL_MM_H

#include <base.h>
#include <mm/heap.h>
#include <mm/mm.h>
#include <mm/vm.h>

#define kernel_phys_to_virt(x) (KERNEL_OFFSET + (x))

#define virt_to_phys(ptr) vm_virt_to_phys((uintptr_t)(ptr))
#define virt_to_phys_ptr(ptr) ((void *) vm_virt_to_phys((uintptr_t)(ptr)))

static inline page_t *alloc_pages(size_t count, uint16_t flags) {
  if (count == 0)
    return NULL;

  page_t *pages = mm_alloc_pages(ZONE_NORMAL, count, flags);
  if (pages == NULL)
    return NULL;

  void *ptr = vm_map_page(pages);
  if (ptr == NULL) {
    mm_free_page(pages);
    return NULL;
  }
  return pages;
}

static inline page_t *alloc_page(uint16_t flags) {
  return alloc_pages(1, flags);
}

static inline page_t *alloc_zero_pages(size_t count, uint16_t flags) {
  page_t *pages = alloc_pages(count, flags);
  if (pages == NULL)
    return NULL;

  memset((void *) pages->addr, 0, PAGES_TO_SIZE(count));
  return pages;
}

static inline page_t *alloc_zero_page(uint16_t flags) {
  return alloc_zero_pages(1, flags);
}

static inline void free_pages(page_t *pages) {
  if (pages == NULL)
    return;

  if (pages->flags.present) {
    vm_unmap_page(pages);
  }
  mm_free_page(pages);
}

static inline void map_pages(page_t *pages) {
  if (pages == NULL)
    return;

  void *ptr = vm_map_page(pages);
  if (ptr == NULL) {
    panic("[mm] remap failed");
    return;
  }
}

static inline void unmap_pages(page_t *pages) {
  if (pages == NULL)
    return;
  vm_unmap_page(pages);
}

#endif
