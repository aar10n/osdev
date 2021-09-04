//
// Created by Aaron Gill-Braun on 2020-09-30.
//

#ifndef KERNEL_MM_HEAP_H
#define KERNEL_MM_HEAP_H

#include <mm/mm.h>
#include <queue.h>

#define CHUNK_MIN_SIZE   8
#define CHUNK_MAX_SIZE   8192
#define CHUNK_SIZE_ALIGN 8
#define CHUNK_MIN_ALIGN  4

#define CHUNK_MAGIC 0xABCD
#define HOLE_MAGIC 0xFACE

// 16 bytes
typedef struct chunk {
  uint16_t magic;                // magic number
  uint16_t size;                 // chunk size
  uint16_t prev_size;            // prev chunk size
  uint16_t free : 1;             // chunk free/used
  uint16_t prev_free : 1;        // prev chunk free/used
  uint16_t : 14;                 // reserved
  LIST_ENTRY(struct chunk) list; // a pointer to the next used chunk
} chunk_t;
static_assert(sizeof(chunk_t) == 24);

typedef struct heap {
  // page_t *source;       // the source of the heap memory
  uintptr_t start_addr; // the heap base address
  uintptr_t end_addr;   // the heap end address
  uintptr_t max_addr;   // the largest allocated address
  size_t size;          // the size of the heap
  size_t used;          // the total number of bytes used
  chunk_t *last_chunk;  // the last created chunk
  // chunk_t *chunks;      // a linked list of free chunks
  LIST_HEAD(chunk_t) chunks; // a linked list of free chunks
} heap_t;

void kheap_init();

void *kmalloc(size_t size) __malloc_like;
void *kmalloca(size_t size, size_t alignment) __malloc_like;
void kfree(void *ptr);
void *kcalloc(size_t nmemb, size_t size) __malloc_like;
void *krealloc(void *ptr, size_t size) __malloc_like;

bool is_kheap_ptr(void *ptr);

// dirty hack until we have a better allocator for smaller
// chunks of identity mapped memory
#define heap_ptr_phys(ptr) ((uintptr_t)(ptr) - KERNEL_OFFSET)
#define heap_ptr_virt(ptr) ((uintptr_t)(ptr) + KERNEL_OFFSET)

#endif
