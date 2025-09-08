//
// Created by Aaron Gill-Braun on 2020-09-30.
//

#include <kernel/mm/heap.h>
#include <kernel/mm/init.h>
#include <kernel/mm/pgtable.h>

#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/panic.h>
#include <kernel/mutex.h>

#include <fs/procfs/procfs.h>


#define END_ADDR(heap) ((heap)->virt_addr + (heap)->size)

mm_heap_t kheap;
mtx_t kheap_lock;

static const char *hist_labels[9] = {
  "0-8", "9-16", "17-32", "33-64", "65-128", "129-512", "513-1024", "larger"
};


static inline int get_hist_bucket(size_t size) {
  switch (size) {
    case 1 ... 8: return 0;
    case 9 ... 16: return 1;
    case 17 ... 32: return 2;
    case 33 ... 64: return 3;
    case 65 ... 128: return 4;
    case 129 ... 512: return 5;
    case 513 ... 1024: return 6;
    default: return 7;
  }
}

static inline mm_chunk_t *get_prev_chunk(mm_chunk_t *chunk) {
  if (chunk->prev_offset == 0) {
    return NULL;
  }

  mm_chunk_t *prev = offset_ptr(chunk, -chunk->prev_offset);
  if (prev->magic != CHUNK_MAGIC) {
    panic("[get_prev_chunk] chunk magic is invalid");
  }
  return prev;
}

static inline mm_chunk_t *get_next_chunk(mm_heap_t *heap, mm_chunk_t *chunk) {
  if (chunk == heap->last_chunk) {
    return NULL;
  }

  uintptr_t next_addr = offset_addr(chunk, sizeof(mm_chunk_t) + chunk->size);
  if (next_addr < END_ADDR(heap) && ((uint16_t *) next_addr)[0] == HOLE_MAGIC) {
    uint16_t hole_size = ((uint16_t *) next_addr)[1];
    next_addr += hole_size;
  }

  if (next_addr >= END_ADDR(heap)) {
    return NULL;
  }

  mm_chunk_t *next = (void *) next_addr;
  if (next->magic != CHUNK_MAGIC) {
    panic("[get_next_chunk] chunk magic is invalid");
  }
  return next;
}

static inline void aquire_heap(mm_heap_t *heap) {
  // curthread will only be null for the earliest of allocations
  if (__expect_false(curthread == NULL)) {
    mtx_spin_lock(&kheap_lock);
  } else {
    mtx_spin_lock(&heap->lock);
  }
}

static inline void release_heap(mm_heap_t *heap) {
  if (__expect_false(curthread == NULL)) {
    mtx_spin_unlock(&kheap_lock);
  } else {
    mtx_spin_unlock(&heap->lock);
  }
}

// ----- heap creation -----

void mm_init_kheap() {
  size_t page_count = SIZE_TO_PAGES(KERNEL_HEAP_SIZE);
  uintptr_t phys_addr = mm_early_alloc_pages(page_count);
  uintptr_t virt_addr = KERNEL_HEAP_VA;
  if (KERNEL_HEAP_SIZE >= BIGPAGE_SIZE && is_aligned(phys_addr, BIGPAGE_SIZE)) {
    uintptr_t num_bigpages = KERNEL_HEAP_SIZE / BIGPAGE_SIZE;
    page_count = PAGES_TO_SIZE(page_count) % BIGPAGE_SIZE;
    early_map_entries(virt_addr, phys_addr, num_bigpages, VM_RDWR|VM_HUGE_2MB);
    phys_addr += num_bigpages * BIGPAGE_SIZE;
    virt_addr += num_bigpages * BIGPAGE_SIZE;
  }

  if (page_count > 0) {
    early_map_entries(virt_addr, phys_addr, page_count, VM_RDWR);
  }

  memset(&kheap, 0, sizeof(mm_heap_t));
  kheap.phys_addr = phys_addr;
  kheap.virt_addr = KERNEL_HEAP_VA;
  kheap.size = KERNEL_HEAP_SIZE;
  kheap.used = 0;
  kheap.last_chunk = NULL;
  LIST_INIT(&kheap.chunks);
  mtx_init(&kheap_lock, MTX_SPIN|MTX_RECURSIVE, "kheap_lock");
  mtx_init(&kheap.lock, MTX_SPIN|MTX_RECURSIVE, "kheap_mutex");

  kprintf("initialized kernel heap\n");
}

uintptr_t kheap_phys_addr() {
  return kheap.phys_addr;
}

// ----- kmalloc -----

void *__kmalloc(mm_heap_t *heap, size_t size, size_t alignment) {
  kassert(heap != NULL);
  if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
    panic("[kmalloc] invalid alignment given: %zu\n", alignment);
  }

  if (size == 0) {
    return NULL;
  } else if (size > CHUNK_MAX_SIZE) {
    panic("[kmalloc] error - request too large (%zu)\n", size);
  }

  aquire_heap(heap);
  heap->stats.alloc_count++;
  heap->stats.alloc_sizes[get_hist_bucket(size)]++;
  size = align(max(size, CHUNK_MIN_SIZE), CHUNK_SIZE_ALIGN);

  // search for the best fitting chunk. If one is not found,
  // we will create a new chunk in the unmanaged heap memory.
  if (LIST_FIRST(&heap->chunks)) {
    // kprintf("[kmalloc] searching used chunks\n");
    mm_chunk_t *chunk = NULL;
    mm_chunk_t *curr = NULL;
    LIST_FOREACH(curr, &heap->chunks, list) {
      // check if chunk is properly aligned
      if (offset_addr(curr, sizeof(mm_chunk_t)) % alignment != 0) {
        continue;
      }

      if (curr->size >= size) {
        if (chunk == NULL || (curr->size < chunk->size)) {
          chunk = curr;
        }
      } else if (curr->size == size) {
        // if current chunk is exact match use it right away
        chunk = curr;
        break;
      }
    }

    if (chunk != NULL) {
      // kprintf("[kmalloc] used chunk found (size %d)\n", chunk->size);
      // if a chunk was found remove it from the list
      LIST_REMOVE(&heap->chunks, chunk, list);
      chunk->free = false;

      heap->used += size + sizeof(mm_chunk_t);
      release_heap(heap);
      return offset_ptr(chunk, sizeof(mm_chunk_t));
    }
  }

  // kprintf("[kmalloc] creating new chunk\n");
  // if we get this far it means that no existing chunk could
  // fit the requested size therefore a new chunk must be made.

  // create new chunk
  uintptr_t chunk_addr;
  if (heap->last_chunk == NULL) {
    chunk_addr = heap->virt_addr;
  } else {
    chunk_addr = offset_addr(heap->last_chunk, sizeof(mm_chunk_t) + heap->last_chunk->size);
  }

  // fix alignment
  uintptr_t aligned_mem = align(chunk_addr + sizeof(mm_chunk_t), alignment);
  uintptr_t aligned_chunk = aligned_mem - sizeof(mm_chunk_t);

  if (aligned_chunk != chunk_addr) {
    size_t hole_size = aligned_chunk - chunk_addr;
    if (hole_size < sizeof(mm_chunk_t) + CHUNK_MIN_SIZE) {
      // we can't create a new chunk here, so we need to create a hole
      ((uint16_t *) chunk_addr)[0] = HOLE_MAGIC;
      ((uint16_t *) chunk_addr)[1] = hole_size;
      heap->used += hole_size;
    } else {
      // create a new free chunk
      mm_chunk_t *free_chunk = (void *) chunk_addr;
      free_chunk->magic = CHUNK_MAGIC;
      free_chunk->size = hole_size - sizeof(mm_chunk_t);
      free_chunk->free = true;
      LIST_ADD_FRONT(&heap->chunks, free_chunk, list);
    }
  }

  if (aligned_mem + size > END_ADDR(heap)) {
    kprintf("heap: allocation overflows end of heap: %p (size=%zu, align=%zu)\n", aligned_mem, size, alignment);
    kprintf("heap: heap out of memory\n");
    kprintf("      virt_addr = %p\n", heap->virt_addr);
    kprintf("      size = %zu\n", heap->size);
    kprintf("      used = %zu\n", heap->used);
    kprintf("      alloc count = %zu\n", heap->stats.alloc_count);
    kprintf("      free count = %zu\n", heap->stats.free_count);

    kprintf("      request_sizes:\n");
    for (int i = 0; i < ARRAY_SIZE(hist_labels); i++) {
      kprintf("        %s - %zu\n", hist_labels[i], heap->stats.alloc_sizes[i]);
    }

    panic("[kmalloc] error - out of memory");
  }

  mm_chunk_t *chunk = (void *) aligned_chunk;
  chunk->magic = CHUNK_MAGIC;
  chunk->size = size;
  chunk->free = false;
  chunk->list.next = NULL;
  chunk->list.prev = NULL;
  if (heap->last_chunk != NULL) {
    // chunk->prev_size = heap->last_chunk->size;
    // chunk->prev_free = heap->last_chunk->free;
    chunk->prev_offset = aligned_chunk - (uintptr_t) heap->last_chunk;
  } else {
    // chunk->prev_size = 0;
    // chunk->prev_free = false;
    chunk->prev_offset = 0;
  }

  heap->last_chunk = chunk;
  heap->used += size + sizeof(mm_chunk_t);

  release_heap(heap);
  return offset_ptr(chunk, sizeof(mm_chunk_t));
}

void *kmalloc(size_t size) {
  return __kmalloc(&kheap, size, CHUNK_MIN_ALIGN);
}

void *kmallocz(size_t size) {
  void *p = __kmalloc(&kheap, size, CHUNK_MIN_ALIGN);
  memset(p, 0, size);
  return p;
}

void *kmalloc_cp(const void *obj, size_t size) {
  if (obj == NULL) {
    return NULL;
  }

  void *p = __kmalloc(&kheap, size, CHUNK_MIN_ALIGN);
  memcpy(p, obj, size);
  return p;
}

void *kmalloca(size_t size, size_t alignment) {
  return __kmalloc(&kheap, size, alignment);
}

// ----- kfree -----

void __kfree(mm_heap_t *heap, void *ptr) {
  kassert(heap != NULL);
  if (ptr == NULL) {
    return;
  }

  heap->stats.free_count++;

  mm_chunk_t *chunk = offset_ptr(ptr, -sizeof(mm_chunk_t));
  if (chunk->magic != CHUNK_MAGIC) {
    kprintf("[kfree] invalid pointer\n");
    return;
  } else if (chunk->free) {
    kprintf("[kfree] freeing already freed chunk\n");
    return;
  }

  aquire_heap(heap);
  if (!(LIST_NEXT(chunk, list) == NULL && LIST_PREV(chunk, list) == NULL)) {
    panic("[kfree] error - chunk linked to other chunks");
  }

  // kprintf("[kfree] freeing pointer %p\n", ptr);
  chunk->free = true;
  mm_chunk_t *next_chunk = get_next_chunk(heap, chunk);
  if (next_chunk != NULL) {
    // next_chunk->prev_free = true;
  }

  LIST_ADD_FRONT(&heap->chunks, chunk, list);
  heap->used -= chunk->size + sizeof(mm_chunk_t);
  release_heap(heap);
}

void kfree(void *ptr) {
  __kfree(&kheap, ptr);
}

// ----- kcalloc -----

void *kcalloc(size_t nmemb, size_t size) {
   if (nmemb == 0 || size == 0) {
    return NULL;
  }

  size_t total = nmemb * size;
  if (total > CHUNK_MAX_SIZE) {
    panic("[kcalloc] error - request too large (%zu, %zu)\n", nmemb, size);
  }

  void *ptr = kmalloc(total);
  if (ptr) {
    memset(ptr, 0, total);
  }
  return ptr;
}

// --------------------

int kheap_is_valid_ptr(void *ptr) {
  uintptr_t chunk_addr = offset_addr(ptr, -sizeof(mm_chunk_t));
  if (chunk_addr < kheap.virt_addr || chunk_addr > END_ADDR(&kheap)) {
    return false;
  }

  mm_chunk_t *chunk = (void *) chunk_addr;
  if (chunk->magic != CHUNK_MAGIC) {
    return false;
  }
  return true;
}

uintptr_t kheap_ptr_to_phys(void *ptr) {
  if ((uintptr_t) ptr == KERNEL_HEAP_VA) {
    return kheap.phys_addr;
  }

  kassert(kheap_is_valid_ptr(ptr));
  size_t offset = ((uintptr_t) ptr) - kheap.virt_addr;
  return kheap.phys_addr + offset;
}

//

void kheap_dump_stats() {
  kprintf_raw("  size = %zu\n", kheap.size);
  kprintf_raw("  used = %zu\n", kheap.used);
  kprintf_raw("  alloc count = %zu\n", kheap.stats.alloc_count);
  kprintf_raw("  free count = %zu\n", kheap.stats.free_count);

  kprintf_raw("  request_sizes:\n");
  for (int i = 0; i < 8; i++) {
    kprintf_raw("    %s - %zu\n", hist_labels[i], kheap.stats.alloc_sizes[i]);
  }
}

// MARK: Procfs Interface

static int kheap_stats_show(seqfile_t *sf, void *_) {
  seq_puts(sf, "kheap stats:\n");
  seq_printf(sf, "  size = %zu\n", kheap.size);
  seq_printf(sf, "  used = %zu\n", kheap.used);
  seq_printf(sf, "  alloc count = %zu\n", kheap.stats.alloc_count);
  seq_printf(sf, "  free count = %zu\n", kheap.stats.free_count);
  seq_puts(sf, "  request_sizes:\n");
  for (int i = 0; i < ARRAY_SIZE(hist_labels); i++) {
    seq_printf(sf, "    %s - %zu\n", hist_labels[i], kheap.stats.alloc_sizes[i]);
  }
  return 0;
}
PROCFS_REGISTER_SIMPLE(kheap_stats, "/kheap_stats", kheap_stats_show, NULL, 0444);
