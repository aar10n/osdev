//
// Created by Aaron Gill-Braun on 2020-09-30.
//

#ifndef KERNEL_MM_HEAP_H
#define KERNEL_MM_HEAP_H

#include <base.h>
#include <queue.h>

#define CHUNK_MIN_SIZE   8
#define CHUNK_MAX_SIZE   8192
#define CHUNK_SIZE_ALIGN 8
#define CHUNK_MIN_ALIGN  4

#define CHUNK_MAGIC 0xC0DE
#define HOLE_MAGIC 0xDEAD

typedef struct mm_chunk {
  uint16_t magic;                   // magic number
  uint16_t size : 15;               // size of chunk
  uint16_t free : 1;                // chunk free/used
  uint16_t prev_size : 15;          // previous chunk size
  uint16_t prev_free : 1;           // previous chunk free/used
  uint16_t prev_offset;             // offset to previous chunk
  LIST_ENTRY(struct mm_chunk) list; // links to free chunks (if free)
} mm_chunk_t;
static_assert(sizeof(mm_chunk_t) == 24);

typedef struct mm_heap {
  uintptr_t phys_addr;  // physical address of heap
  uintptr_t start_addr; // the heap base address
  uintptr_t end_addr;   // the heap end address
  size_t size;          // the size of the heap
  size_t used;          // the total number of bytes used
  mm_chunk_t *last_chunk;  // the last created chunk
  LIST_HEAD(mm_chunk_t) chunks; // a linked list of free chunks
} mm_heap_t;

void mm_init_kheap();
void kheap_init();

void *kmalloc(size_t size) __malloc_like;
void *kmalloca(size_t size, size_t alignment) __malloc_like;
void kfree(void *ptr);
void *kcalloc(size_t nmemb, size_t size) __malloc_like;

int kheap_is_valid_ptr(void *ptr);
uintptr_t kheap_ptr_to_phys(void *ptr);

#endif
