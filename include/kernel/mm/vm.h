//
// Created by Aaron Gill-Braun on 2020-10-03.
//

#ifndef KERNEL_MM_VM_H
#define KERNEL_MM_VM_H

#include <mm/mm.h>

#define entry_to_table(entry) \
  ((uint64_t *) phys_to_virt((entry) & 0xFFFFFFFFFF000))

#define entry_to_addr(entry) \
  ((entry) & (~0xFFF))

#define R_ENTRY 510ULL
#define TEMP_ENTRY 511L
#define TEMP_PAGE 0xFFFFFFFFFFFFF000

typedef struct vm_area {
  uintptr_t base;
  size_t size;
  page_t *pages;
} vm_area_t;

void vm_init();
void *vm_map_page(page_t *page);
void *vm_map_addr(uintptr_t phys_addr, size_t len, uint16_t flags);
void *vm_map_vaddr(uintptr_t virt_addr, uintptr_t phys_addr, size_t len, uint16_t flags);

#endif
