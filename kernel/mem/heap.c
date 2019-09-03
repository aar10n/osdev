//
// Created by Aaron Gill-Braun on 2019-05-28.
//

#include <stddef.h>
#include <stdint.h>
#include "../../libc/stdio.h"
#include "../../libc/string.h"
#include "heap.h"

#define HEAP_BASE 0xC0300000
#define HEAP_MAX  0xC0400000

static uintptr_t heap_ptr = HEAP_BASE;


// _kmalloc - Initial Kernel Heap Allocator
void *_kmalloc(size_t size) {
  uintptr_t addr = heap_ptr;
  size_t offset = size + (8 - (size % 8));
  heap_ptr += offset;
  if (heap_ptr >= HEAP_MAX) {
    // panic - Out of memory!
    kprintf("fatal error - out of memory!\n");
    return NULL;
  }

  return (void *) addr;
}
