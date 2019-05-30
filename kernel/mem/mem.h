//
// Created by Aaron Gill-Braun on 2019-04-29.
//

#ifndef KERNEL_MEM_MEM_H
#define KERNEL_MEM_MEM_H

#include <stdbool.h>
#include "multiboot.h"

#define KERNEL_BASE 0xC0000000
#define PAGE_SIZE 4096
#define MAX_ORDER 11

#define ptov(addr) ((addr) + KERNEL_BASE)
#define vtop(addr) ((addr) - KERNEL_BASE)

#define flag_get(flags, flag) ((flags >> flag) & 1)
#define flag_set(flags, flag) (flags |= (1 << flag))

#define addr_to_pde(addr) ((unsigned int)(addr >> 22))
#define addr_to_pte(addr) ((unsigned int)(addr >> 12 & 0x03FF))

#define BLOCK_IS_TOP  0
#define BLOCK_IS_HEAD 2
#define BLOCK_IS_FREE 4

//
//
//

extern uint32_t _kernel_start;
extern uint32_t _kernel_end;
extern uint32_t _page_directory;
extern uint32_t _initial_page_table;

#define kernel_start (ptov((uint32_t) &_kernel_start))
#define kernel_end ((uint32_t) &_kernel_end)
#define page_directory ((uint32_t *) &_page_directory)
#define initial_page_table ((uint32_t *) &_initial_page_table)

typedef struct page {
  void  *addr;
  size_t size;
} page_t;

void mem_init(uint32_t base_addr, size_t length);
void mem_split(unsigned int order);

page_t *alloc_pages(unsigned int order);

#endif //KERNEL_MEM_MEM_H
