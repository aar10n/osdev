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

#define AREA_ATTR_MASK 0xF7

// area type
#define AREA_USED     0x01  // area is being used
#define AREA_RESERVED 0x02  // area has been reserved
#define AREA_MMIO     0x04  // area is memory mapped io
#define AREA_UNUSABLE 0x08  // area is unusable
// assoc data
#define AREA_PHYS     0x10  // area uses physical address
#define AREA_PAGE     0x20  // area uses pages
#define AREA_FILE     0x40  // area is an mmap'd file

#define PF_PRESENT  0x01
#define PF_WRITE    0x02
#define PF_USER     0x04
#define PF_RESWRITE 0x08
#define PF_INSFETCH 0x10

typedef struct file file_t;

typedef struct vm {
  uint64_t *pml4;
  intvl_tree_t *tree;
  uint64_t *temp_dir;
} vm_t;

typedef struct vm_area {
  uintptr_t base;
  size_t size;
  union {
    void *data;
    uintptr_t phys;
    page_t *pages;
    file_t *file;
  };
  uint32_t attr;
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

uintptr_t vm_reserve(size_t len);
void vm_mark_reserved(uintptr_t virt_addr, size_t len);

void vm_update_page(page_t *page, uint16_t flags);
void vm_update_pages(page_t *page, uint16_t flags);

void vm_unmap_page(page_t *page);
void vm_unmap_vaddr(uintptr_t virt_addr);

page_t *vm_get_page(uintptr_t addr);
vm_area_t *vm_get_vm_area(uintptr_t addr);
int vm_attach_page(uintptr_t addr, page_t *page);
int vm_attach_file(uintptr_t addr, file_t *file);
bool vm_find_free_area(vm_search_t search_type, uintptr_t *addr, size_t len);

void vm_print_debug_mappings();


static inline intptr_t vm_virt_to_phys(uintptr_t addr) {
  vm_area_t *area = vm_get_vm_area(addr);
  if (area == NULL) {
    return -1;
  }

  uintptr_t offset = addr - area->base;
  if (area->attr & AREA_PHYS) {
    return area->phys + offset;
  } else if (area->attr & AREA_PAGE) {
    page_t *page = area->pages;
    while (offset >= PAGE_SIZE) {
      size_t size = PAGE_SIZE;
      if (page->flags.page_size_2mb) {
        size = PAGE_SIZE_2MB;
      } else if (page->flags.page_size_1gb) {
        size = PAGE_SIZE_1GB;
      }

      offset -= size;
      page = page->next;
    }
    return page->frame + offset;
  }
  return -1;
}


#endif
