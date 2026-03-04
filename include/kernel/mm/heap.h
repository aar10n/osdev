//
// Created by Aaron Gill-Braun on 2020-09-30.
//

#ifndef KERNEL_MM_HEAP_H
#define KERNEL_MM_HEAP_H

#include <kernel/base.h>
#include <kernel/mutex.h>
#include <kernel/string.h>

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
  void *caller;                     // return address of allocator
  LIST_ENTRY(struct mm_chunk) list; // links to free chunks (if free)
} mm_chunk_t;
static_assert(sizeof(mm_chunk_t) == 32);

typedef struct mm_heap {
  uintptr_t phys_addr;          // physical address of heap
  uintptr_t virt_addr;          // virtual address of heap base
  mm_chunk_t *last_chunk;       // the last created chunk
  LIST_HEAD(mm_chunk_t) chunks; // linked list of free chunks
  mtx_t lock;                   // heap lock (must be held to alloc/free)

  size_t size;                  // the size of the heap
  size_t used;                  // the total number of bytes used
  struct {
    size_t alloc_count;         // the number of times malloc was called
    size_t free_count;          // the number of times free was called
    size_t alloc_sizes[9];      // a histogram of alloc request sizes
  } stats;
} mm_heap_t;

void mm_init_kheap();
uintptr_t kheap_phys_addr();

void *_kmalloc(size_t size, size_t align, void *caller) _malloc_like;
void kfree(void *ptr);

#define kmalloc(size)       _kmalloc(size, CHUNK_SIZE_ALIGN, __builtin_return_address(0))
#define kmallocz(size)      ({ size_t _sz = (size); void *_p = kmalloc(_sz); if (_p) memset(_p, 0, _sz); _p; })
#define kmalloca(size, a)   _kmalloc(size, a, __builtin_return_address(0))
#define kcalloc(n, sz)      ({ size_t _n = (n), _s = (sz); size_t _t = _n * _s; \
                               kassert(_n == 0 || _t / _n == _s); kmallocz(_t); })
#define kmalloc_cp(p, sz)   ({ void *_p = kmalloc(sz); if (_p && (p)) memcpy(_p, (p), sz); _p; })

#define kfreep(ptr) do { \
  kfree(*(ptr)); \
  *(ptr) = NULL; \
} while (0)

void kheap_dump_stats();

int kheap_is_valid_ptr(void *ptr);
uintptr_t kheap_ptr_to_phys(void *ptr);

#endif
