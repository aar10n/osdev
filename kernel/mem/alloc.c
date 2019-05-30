//
// Created by Aaron Gill-Braun on 2019-05-28.
//

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "alloc.h"

#define HEAP_BASE 0xC0400000
#define HEAP_MAX  0xC0500000

static void *heap_ptr = (void *) HEAP_BASE;


// _kmalloc - Initial Kernel Heap Allocator
void *_kmalloc(size_t size) {
  uintptr_t addr = (uintptr_t) heap_ptr;
  size_t offset = size + (8 - (size % 8));
  heap_ptr += offset;
  if (heap_ptr >= (void *) HEAP_MAX) {
    // panic - Out of memory!
    kprintf("fatal error - out of memory!\n");
    return NULL;
  }

  return (void *) addr;
}
