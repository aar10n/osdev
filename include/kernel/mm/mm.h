//
// Created by Aaron Gill-Braun on 2020-09-30.
//

#ifndef KERNEL_MM_MM_H
#define KERNEL_MM_MM_H

#include <base.h>

#define KERNEL_VA 0xFFFFFF8000100000
#define STACK_VA 0xFFFFFFA000000000

#define PAGE_SHIFT 12
#define PAGE_SIZE (1 << PAGE_SHIFT)

#define PAGE_SHIFT_2MB 21
#define PAGE_SIZE_2MB (1 << PAGE_SHIFT_2MB)

#define MAX_ORDER 11

#define virt_to_phys(x) (((x) + kernel_phys) - KERNEL_VA)
#define phys_to_virt(x) (KERNEL_VA - (kernel_phys - (x)))

#define virt_addr(pml4e, pdpe, pde, pte) \
  (((pml4e##ULL) << 39) | ((pdpe##ULL) << 30) | \
  ((pde##ULL) << 21) | ((pte##ULL) << 12) | \
  (0xFFFFULL << 48))

#define pt_index(a) (((a) >> 12) & 0x1FF)
#define pdt_index(a) (((a) >> 21) & 0x1FF)
#define pdpt_index(a) (((a) >> 30) & 0x1FF)
#define pml4_index(a) (((a) >> 39) & 0x1FF)


typedef enum zone_type {
  ZONE_RESERVED, // non-usable
  ZONE_LOW,      // below 1MB
  ZONE_DMA,      // between 1MB and 16MB
  ZONE_NORMAL,   // 16MB and up
} zone_type_t;

typedef struct page {
  uintptr_t phys_addr; // physical address (constant)
  uintptr_t virt_addr; // virtual address (not constant)
  union {
    uint16_t raw : 10;
    struct {
      uint16_t free : 1;      // page is free
      uint16_t split : 1;     // page was split
      uint16_t head : 1;      // page is head of split page
      uint16_t tail : 1;      // page is tail of split page
      uint16_t present : 1;   // page is mapped in memory
      uint16_t readwrite : 1; // page is read/write or read-only
      uint16_t user : 1;      // page is for user or supervisor
      uint16_t reserved : 3;
    };
  } flags;
  uint16_t order : 4; // the order (and size) of this page
  zone_type_t zone;   // the zone this page belongs to

  struct page *next;
  struct page *prev;

  struct page *parent;
  struct page *head;
  struct page *tail;
} page_t;

typedef struct mmap_entry {
  zone_type_t type;
  uintptr_t start;
  size_t length;
} mmap_entry_t;

typedef struct free_pages {
  size_t count;
  page_t *first;
} free_pages_t;

typedef struct mem_zone {
  zone_type_t type;
  free_pages_t free_pages[MAX_ORDER];
} mem_zone_t;

//

void mem_init(uintptr_t base_addr, size_t size);
page_t *alloc_pages(int order, uint8_t flags);
page_t *alloc_page(uint8_t flags);
void free_page(page_t *page);

void mm_print_debug_stats();
void mm_print_debug_page(page_t *page);

#endif
