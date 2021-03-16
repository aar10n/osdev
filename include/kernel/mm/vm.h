//
// Created by Aaron Gill-Braun on 2020-10-03.
//

#ifndef KERNEL_MM_VM_H
#define KERNEL_MM_VM_H

#include <base.h>
#include <mm/mm.h>
#include <interval_tree.h>
#include <string.h>

#define VM (PERCPU->vm)

#define U_ENTRY 0ULL
#define R_ENTRY 510ULL
#define K_ENTRY 511ULL
#define TEMP_ENTRY 511L
#define TEMP_PAGE 0xFFFFFFFFFFFFF000

#define LOW_HALF_START 0x0000000000000000
#define LOW_HALF_END 0x00007FFFFFFFFFFF
#define HIGH_HALF_START 0xFFFF800000000000
#define HIGH_HALF_END 0xFFFFFFFFFFFFFFFF

#define PF_PRESENT  0x01
#define PF_WRITE    0x02
#define PF_USER     0x04
#define PF_RESWRITE 0x08
#define PF_INSFETCH 0x10

typedef struct vm {
  uint64_t *pml4;
  intvl_tree_t *tree;
  uint64_t *temp_dir;
} vm_t;

typedef struct vm_area {
  uintptr_t base;
  size_t size;
  page_t *pages;
} vm_area_t;

typedef enum {
  EXACTLY,
  ABOVE,
  BELOW,
} vm_search_t;

void vm_init();
vm_t *vm_duplicate();
void *vm_create_ap_tables();
void vm_swap_vmspace(vm_t *new_vm);

void *vm_map_page(page_t *page);
void *vm_map_page_vaddr(uintptr_t virt_addr, page_t *page);
void *vm_map_page_search(page_t *page, vm_search_t search_type, uintptr_t vaddr);
void *vm_map_addr(uintptr_t phys_addr, size_t len, uint16_t flags);
void *vm_map_vaddr(uintptr_t virt_addr, uintptr_t phys_addr, size_t len, uint16_t flags);

void vm_update_page(page_t *page, uint16_t flags);
void vm_update_pages(page_t *page, uint16_t flags);

void vm_unmap_page(page_t *page);
void vm_unmap_vaddr(uintptr_t virt_addr);

page_t *vm_get_page(uintptr_t addr);
vm_area_t *vm_get_vm_area(uintptr_t addr);
bool vm_find_free_area(vm_search_t search_type, uintptr_t *addr, size_t len);

void vm_print_debug_mappings();


static inline page_t *alloc_zero_pages(size_t count, uint16_t flags) {
  if (count == 0) {
    return NULL;
  }

  page_t *pages = mm_alloc_pages(ZONE_NORMAL, count, flags);
  if (pages == NULL) {
    return NULL;
  }

  void *ptr = vm_map_page(pages);
  if (ptr == NULL) {
    return NULL;
  }

  memset(ptr, 0, PAGES_TO_SIZE(count));
  return pages;
}

static inline page_t *alloc_zero_page(uint16_t flags) {
  return alloc_zero_pages(1, flags);
}


#endif
