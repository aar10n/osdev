//
// Created by Aaron Gill-Braun on 2020-10-03.
//

#ifndef KERNEL_MM_VM_H
#define KERNEL_MM_VM_H

#include <mm/mm.h>
#include <interval_tree.h>

#define VM (PERCPU->vm)

#define R_ENTRY 510ULL
#define K_ENTRY 511ULL
#define TEMP_ENTRY 511L
#define TEMP_PAGE 0xFFFFFFFFFFFFF000

#define LOW_HALF_START 0x0000000000000000
#define LOW_HALF_END 0x00007FFFFFFFFFFF
#define HIGH_HALF_START 0xFFFF800000000000
#define HIGH_HALF_END 0xFFFFFFFFFFFFFFFF

#define entry_to_table(entry) \
  ((uint64_t *) phys_to_virt((entry) & 0xFFFFFFFFFF000))


typedef struct {
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
void *vm_create_ap_tables();
void *vm_map_page(page_t *page);
void *vm_map_page_vaddr(uintptr_t virt_addr, page_t *page);
void *vm_map_addr(uintptr_t phys_addr, size_t len, uint16_t flags);
void *vm_map_vaddr(uintptr_t virt_addr, uintptr_t phys_addr, size_t len, uint16_t flags);

vm_area_t *vm_get_vm_area(uintptr_t address);
bool vm_find_free_area(vm_search_t search_type, uintptr_t *addr, size_t len);

#endif
