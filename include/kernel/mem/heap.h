//
// Created by Aaron Gill-Braun on 2019-05-28.
//

#ifndef KERNEL_MEM_HEAP_H
#define KERNEL_MEM_HEAP_H

#include <kernel/mem/mm.h>

// The simple heap is a 1 MiB heap used to bootstrap
// the memory allocator during startup. We only need
// enough memory to allocate a couple of page_t structs
// so 1 MiB should be more than enough.
#define SIMPLE_HEAP_BASE 0xC020F000
#define SIMPLE_HEAP_MAX 0xC030F000

#define HEAP_MIN_SIZE 0x1000   // 4 KiB
#define HEAP_MAX_SIZE 0x400000 // 4 MiB

#define CHUNK_MIN_SIZE 8
#define CHUNK_MAX_SIZE 8192

#define CHUNK_MAGIC 0xABCD
#define HOLE_MAGIC 0xFACE

// 8 bytes
typedef struct chunk {
  uint16_t magic;     // magic number
  uint8_t size : 7; // chunk size in the form of 2^n
  uint8_t free : 1; // is chunk free
  // when chunk is used
  union {
    uint8_t size : 7; // last chunk size in form of 2^n
    uint8_t free : 1; // is last chunk free
  } last;
  // when chunk is free
  struct chunk *next;     // a pointer to the next free chunk
} chunk_t;

typedef struct heap {
  page_t *source;       // the source of the heap memory
  uintptr_t start_addr; // the heap base address
  uintptr_t end_addr;   // the heap end address
  size_t size;          // the size of the heap
  chunk_t *last_chunk;  // the last created chunk
  chunk_t *chunks;      // a linked list of free chunks
} heap_t;

void kheap_init();
heap_t *create_heap(uintptr_t base_addr, size_t size);

void *kmalloc(size_t size);
void kfree(void *ptr);
void *kcalloc(size_t nmemb, size_t size);
void *krealloc(void *ptr, size_t size);

#endif // KERNEL_MEM_HEAP_H
