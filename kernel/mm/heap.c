//
// Created by Aaron Gill-Braun on 2020-09-30.
//

#include <mm/heap.h>
#include <mm/init.h>
#include <mm/pgtable.h>

#include <printf.h>
#include <string.h>
#include <spinlock.h>
#include <panic.h>
#include <mutex.h>

mm_heap_t kheap;
spinlock_t kheap_lock;
mutex_t kheap_mutex;


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

static inline mm_chunk_t *get_next_chunk(mm_chunk_t *chunk) {
  if (chunk == kheap.last_chunk) {
    return NULL;
  }

  uintptr_t next_addr = offset_addr(chunk, sizeof(mm_chunk_t) + chunk->size);
  if (next_addr < kheap.end_addr && ((uint16_t *) next_addr)[0] == HOLE_MAGIC) {
    uint16_t hole_size = ((uint16_t *) next_addr)[1];
    next_addr += hole_size;
  }

  if (next_addr >= kheap.end_addr) {
    return NULL;
  }

  mm_chunk_t *next = (void *) next_addr;
  if (next->magic != CHUNK_MAGIC) {
    panic("[get_next_chunk] chunk magic is invalid");
  }
  return next;
}

static inline void aquire_kheap() {
  if (PERCPU_THREAD == NULL) {
    spin_lock(&kheap_lock);
  } else {
    mutex_lock(&kheap_mutex);
  }
}

static inline void release_kheap() {
  if (PERCPU_THREAD == NULL) {
    spin_unlock(&kheap_lock);
  } else {
    mutex_unlock(&kheap_mutex);
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
    early_map_entries(virt_addr, phys_addr, num_bigpages, PG_WRITE | PG_BIGPAGE);
    phys_addr += num_bigpages * BIGPAGE_SIZE;
    virt_addr += num_bigpages * BIGPAGE_SIZE;
  }

  if (page_count > 0) {
    early_map_entries(virt_addr, phys_addr, page_count, PG_WRITE);
  }

  kheap.phys_addr = phys_addr;
  kheap.start_addr = KERNEL_HEAP_VA;
  kheap.end_addr = KERNEL_HEAP_VA + KERNEL_HEAP_SIZE;
  kheap.size = KERNEL_HEAP_SIZE;
  kheap.used = 0;
  kheap.last_chunk = NULL;
  LIST_INIT(&kheap.chunks);
  spin_init(&kheap_lock);
  mutex_init(&kheap_mutex, MUTEX_REENTRANT | MUTEX_SHARED);

  kprintf("initialized kernel heap\n");
}

// ----- kmalloc -----

void *__kmalloc(size_t size, size_t alignment) {
  if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
    panic("[kmalloc] invalid alignment given: %zu\n", alignment);
  }

  if (size == 0) {
    return NULL;
  } else if (size > CHUNK_MAX_SIZE) {
    panic("[kmalloc] error - request too large (%zu)\n", size);
  }

  aquire_kheap();
  size = align(max(size, CHUNK_MIN_SIZE), CHUNK_SIZE_ALIGN);

  // search for the best fitting chunk. If one is not found,
  // we will create a new chunk in the unmanaged heap memory.
  if (LIST_FIRST(&kheap.chunks)) {
    // kprintf("[kmalloc] searching used chunks\n");
    mm_chunk_t *chunk = NULL;
    mm_chunk_t *curr = NULL;
    LIST_FOREACH(curr, &kheap.chunks, list) {
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
      LIST_REMOVE(&kheap.chunks, chunk, list);
      chunk->free = false;

      kheap.used += size + sizeof(mm_chunk_t);
      release_kheap();
      return offset_ptr(chunk, sizeof(mm_chunk_t));
    }
  }

  // kprintf("[kmalloc] creating new chunk\n");
  // if we get this far it means that no existing chunk could
  // fit the requested size therefore a new chunk must be made.

  // create new chunk
  uintptr_t chunk_addr;
  if (kheap.last_chunk == NULL) {
    chunk_addr = kheap.start_addr;
  } else {
    chunk_addr = offset_addr(kheap.last_chunk, sizeof(mm_chunk_t) + kheap.last_chunk->size);
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
      kheap.used += hole_size;
    } else {
      // create a new free chunk
      mm_chunk_t *free_chunk = (void *) chunk_addr;
      free_chunk->magic = CHUNK_MAGIC;
      free_chunk->size = hole_size - sizeof(mm_chunk_t);
      free_chunk->free = true;
      LIST_ADD_FRONT(&kheap.chunks, free_chunk, list);
    }
  }

  if (aligned_mem + size > kheap.end_addr) {
    panic("[kmalloc] error - out of memory");
  }

  mm_chunk_t *chunk = (void *) aligned_chunk;
  chunk->magic = CHUNK_MAGIC;
  chunk->size = size;
  chunk->free = false;
  chunk->list.next = NULL;
  chunk->list.prev = NULL;
  if (kheap.last_chunk != NULL) {
    // chunk->prev_size = kheap.last_chunk->size;
    // chunk->prev_free = kheap.last_chunk->free;
    chunk->prev_offset = aligned_chunk - (uintptr_t) kheap.last_chunk;
  } else {
    // chunk->prev_size = 0;
    // chunk->prev_free = false;
    chunk->prev_offset = 0;
  }

  kheap.last_chunk = chunk;
  kheap.used += size + sizeof(mm_chunk_t);

  release_kheap();
  return offset_ptr(chunk, sizeof(mm_chunk_t));
}

void *kmalloc(size_t size) {
  return __kmalloc(size, CHUNK_MIN_ALIGN);
}

void *kmalloca(size_t size, size_t alignment) {
  return __kmalloc(size, alignment);
}

// ----- kfree -----

void kfree(void *ptr) {
  if (ptr == NULL) {
    return;
  }

  mm_chunk_t *chunk = offset_ptr(ptr, -sizeof(mm_chunk_t));
  if (chunk->magic != CHUNK_MAGIC) {
    kprintf("[kfree] invalid pointer\n");
    return;
  } else if (chunk->free) {
    kprintf("[kfree] freeing already freed chunk\n");
    return;
  }

  aquire_kheap();
  if (!(LIST_NEXT(chunk, list) == NULL && LIST_PREV(chunk, list) == NULL)) {
    panic("[kfree] error - chunk linked to other chunks");
  }

  // kprintf("[kfree] freeing pointer %p\n", ptr);
  chunk->free = true;
  mm_chunk_t *next_chunk = get_next_chunk(chunk);
  if (next_chunk != NULL) {
    // next_chunk->prev_free = true;
  }

  LIST_ADD_FRONT(&kheap.chunks, chunk, list);
  kheap.used -= chunk->size + sizeof(mm_chunk_t);
  release_kheap();
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
  if (chunk_addr < kheap.start_addr || chunk_addr > kheap.end_addr) {
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
  size_t offset = ((uintptr_t) ptr) - kheap.start_addr;
  return kheap.phys_addr + offset;
}
