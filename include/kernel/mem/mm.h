//
// Created by Aaron Gill-Braun on 2019-04-29.
//

#ifndef KERNEL_MEM_MM_H
#define KERNEL_MEM_MM_H

#include <multiboot.h>
#include <stdbool.h>
#include <stddef.h>

#define KERNEL_BASE 0xC0000000
#define PAGE_SIZE 4096
#define MAX_ORDER 11

#define ptov(addr) (((uintptr_t) addr) + KERNEL_BASE)
#define vtop(addr) (((uintptr_t) addr) - KERNEL_BASE)

#define addr_to_pde(addr) ((unsigned int) ((uintptr_t) addr >> 22))
#define addr_to_pte(addr) ((unsigned int) ((uintptr_t) addr >> 12 & 0x03FF))

/*
 * page status flags
 *
 *   PAGE_FREE  - Page is available
 *   PAGE_USED  - Page is unavailable
 *   PAGE_HEAD  - Page is a head buddy
 *   PAGE_TAIL  - Page is a tail buddy
 *   PAGE_SPLIT - Page is a split page
 */
#define PAGE_FREE 0x00
#define PAGE_USED 0x01
#define PAGE_HEAD 0x02
#define PAGE_TAIL 0x04
#define PAGE_SPLIT 0x08

//
//
//

extern uint32_t _kernel_start;
extern uint32_t _kernel_end;

#define kernel_start (ptov((uint32_t) &_kernel_start))
#define kernel_end ((uint32_t) &_kernel_end)

typedef struct page {
  uintptr_t frame;
  uintptr_t addr;
  size_t size;
  uint8_t flags;
  struct page *next;
  struct page *parent;

  union {
    struct page *head; // PAGE_SPLIT
    struct page *tail; // PAGE_HEAD
  };
} page_t;

void mem_init(uintptr_t base_addr, size_t size);
page_t *alloc_pages(int order);
page_t *alloc_page();
void free_page(page_t *page);

#endif // KERNEL_MEM_MM_H
