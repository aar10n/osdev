//
// Created by Aaron Gill-Braun on 2019-04-29.
//

#include <stdio.h>
#include <stddef.h>
#include <stdbool.h>
#include <math.h>
#include <stdalign.h>
#include "mem.h"
#include "alloc.h"

typedef struct free_block {
  void *page;
  uint16_t flags;
  struct free_block *next;
  struct free_block *last;
} free_block_t;

free_block_t *free[MAX_ORDER];
int block_counts[MAX_ORDER];

static uint32_t *pd;


void mem_distribute(size_t mem_size) {
  size_t available = next_pow2(mem_size) >> 1;
  mem_size -= available;
  while (available != 0) {
    size_t blocks = (available / PAGE_SIZE) / 2;
    for (int i = 0; i < MAX_ORDER; i++) {
      if (blocks == 0) {
        block_counts[i] += 1;
        available = next_pow2(mem_size) >> 1;
        mem_size -= available;
        break;
      } else {
        block_counts[i] += blocks;
        blocks = blocks / 4;
      }
    }
  }
}

void mem_init(uint32_t base_addr, size_t length) {
  pd = page_directory;
  mem_distribute(length);

  uintptr_t page = base_addr;
  for (int i = 0; i < MAX_ORDER; i++) {
    kprintf("%p | page[%u][%u]\n",
            page,
            addr_to_pde(page),
            addr_to_pte(page));

    free[i] = _kmalloc(sizeof(free_block_t));
    free_block_t *block = free[i];
    free_block_t *last = NULL;
    for (int j = 0; j < block_counts[i]; j++) {
      block->next = _kmalloc(sizeof(free_block_t));
      block->last = last;
      block->page = (void *) page;

      last = block;
      block = block->next;
      page += (PAGE_SIZE << i);
    }

    // kprintf("%d | %d x %d KiB blocks\n",
    //     i, block_counts[i], (PAGE_SIZE << i) / 1024);
  }
}

//
//
//

void mem_split(unsigned int order) {
  if (order > MAX_ORDER - 1) {
    // fatal error
    kprintf("fatal error: out of memory\n");
    return;
  } else if (free[order] == NULL) {
    mem_split(order + 1);
  }
}

page_t *alloc_pages(unsigned int order) {
  if (order > MAX_ORDER - 1) {
    // error
    kprintf("error: invalid allocation order\n");
    return NULL;
  }

  if (free[order] == NULL) {
    // split above memory
  }
}
