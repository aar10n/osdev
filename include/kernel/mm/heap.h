//
// Created by Aaron Gill-Braun on 2020-09-30.
//

#ifndef KERNEL_MM_HEAP_H
#define KERNEL_MM_HEAP_H

#include <base.h>
#include <queue.h>
#include <mutex.h>
#include <string.h>

// TODO: switch to better allocator for large sizes
#define CHUNK_MIN_SIZE   8
#define CHUNK_MAX_SIZE   524288
#define CHUNK_SIZE_ALIGN 8
#define CHUNK_MIN_ALIGN  4

#define CHUNK_MAGIC 0xC0DE
#define HOLE_MAGIC 0xDEAD

typedef struct page page_t;

typedef struct mm_chunk {
  uint16_t magic;                   // magic number
  uint16_t prev_offset;             // offset to previous chunk
  uint32_t size : 31;               // size of chunk
  uint32_t free : 1;                // chunk free/used
  LIST_ENTRY(struct mm_chunk) list; // links to free chunks (if free)
} mm_chunk_t;
static_assert(sizeof(mm_chunk_t) == 24);

typedef struct mm_heap {
  uintptr_t phys_addr;          // physical address of heap
  uintptr_t virt_addr;          // virtual address of heap base
  page_t *pages;                // pages representing the heap
  mm_chunk_t *last_chunk;       // the last created chunk
  LIST_HEAD(mm_chunk_t) chunks; // linked list of free chunks
  mutex_t lock;                 // heap lock (must be held to alloc/free)

  size_t size;                  // the size of the heap
  size_t used;                  // the total number of bytes used
  struct {
    size_t alloc_count;         // the number of times malloc was called
    size_t free_count;          // the number of times free was called
    size_t alloc_sizes[9];      // a histogram of alloc request sizes
  } stats;
} mm_heap_t;

void mm_init_kheap();
void kheap_init();

void *kmalloc(size_t size) __malloc_like;
void *kmalloca(size_t size, size_t alignment) __malloc_like;
void kfree(void *ptr);
void *kcalloc(size_t nmemb, size_t size) __malloc_like;

static inline void *kmalloc_z(size_t size) {
  void *p = kmalloc(size);
  memset(p, 0, size);
  return p;
}

void kheap_dump_stats();

int kheap_is_valid_ptr(void *ptr);
uintptr_t kheap_ptr_to_phys(void *ptr);

#endif
