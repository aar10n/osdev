//
// Created by Aaron Gill-Braun on 2020-09-30.
//


#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include <base.h>
#include <panic.h>
#include <mm/heap.h>
#include <mm/mm.h>

static heap_t *kheap = NULL;

//

static inline uintptr_t next_chunk_start() {
  if (kheap->last_chunk == NULL) {
    return kheap->start_addr;
  }

  size_t size = 1 << kheap->last_chunk->size;
  return (uintptr_t) kheap->last_chunk + sizeof(chunk_t) + size;
}

static inline bool is_valid_ptr(void *ptr) {
  uintptr_t chunk_ptr = (uintptr_t) ptr - sizeof(chunk_t);
  if (ptr == NULL || chunk_ptr < kheap->start_addr || chunk_ptr > kheap->end_addr) {
    return false;
  }

  chunk_t *chunk = (chunk_t *) chunk_ptr;
  if (chunk->magic == HOLE_MAGIC) {
    // this should never happen
    kprintf("-- header is a hole --\n");
    kprintf("chunk is a hole\n");
    return false;
  } else if (chunk->magic != CHUNK_MAGIC) {
    // the chunk header is invalid
    kprintf("-- invalid header --\n");
    kprintf("pointer: %p\n", ptr);
    kprintf("magic: 0x%04X\n", chunk->magic);
    return false;
  }

  return true;
}

static inline chunk_t *get_chunk(void *ptr) {
  uintptr_t chunk_ptr = (uintptr_t) ptr - sizeof(chunk_t);
  return (chunk_t *) chunk_ptr;
}

static inline chunk_t *get_next_chunk(chunk_t *chunk) {
  if (chunk == kheap->last_chunk) {
    return NULL;
  }

  size_t size = chunk->magic == HOLE_MAGIC ? 0 : 1 << chunk->size;
  void *next_ptr = (void *) chunk + size + (sizeof(chunk_t) * 2);
  chunk_t *next_chunk = get_chunk(next_ptr);
  if (is_valid_ptr(next_ptr)) {
    return next_chunk;
  } else if (next_chunk && next_chunk->magic == HOLE_MAGIC) {
    return get_next_chunk(next_chunk);
  }
  return NULL;
}

// ----- heap creation -----

void kheap_init() {
  size_t heap_size = boot_info->reserved_size - sizeof(heap_t);

  kheap = (heap_t *) boot_info->reserved_base;
  kheap->start_addr = boot_info->reserved_base + sizeof(heap_t);
  kheap->end_addr = kheap->start_addr + heap_size;
  kheap->size = heap_size;
  kheap->chunks = NULL;
  kheap->last_chunk = NULL;
}

// ----- kmalloc -----

void *kmalloc(size_t size) {
  if (size == 0) {
    return NULL;
  } else if (size > UINTPTR_MAX) {
    // if the requested size is too large, maybe we should
    // fall back to an allocator better suited to large
    // requests.
    kprintf("[kmalloc] request size: %u\n", size);
    kprintf("[kmalloc] error - request too large\n");
    return NULL;
  }

  // otherwise proceed with the normal allocation
  size_t aligned = next_pow2(max(size, CHUNK_MIN_SIZE));
  // kprintf("kmalloc\n");
  // kprintf("size: %u\n", size);
  // kprintf("aligned: %u\n", aligned);

  // first search for the best fitting chunk in the list
  // of used chunks. If one is not found, we will create
  // a new chunk in the unmanaged heap memory.
  if (kheap->chunks) {
    // kprintf("[kmalloc] searching used chunks\n");

    chunk_t *chunk = NULL;
    chunk_t *chunk_last = NULL;

    chunk_t *curr = kheap->chunks;
    chunk_t *last = NULL;
    while (curr) {
      size_t chunk_size = 1 << curr->size;
      if (chunk_size > aligned) {
        if (!chunk || chunk_size < chunk->size) {
          chunk = curr;
          chunk_last = last;
        }
      } else if (chunk_size == aligned) {
        // if current chunk is exact match use it right away
        chunk = curr;
        chunk_last = last;
        break;
      }

      last = curr;
      curr = curr->next;
    }

    if (chunk) {
      // kprintf("[kmalloc] used chunk found (size %d)\n", 1 << chunk->size);
      // if a chunk was found remove it from the list
      if (chunk_last) {
        chunk_last->next = chunk->next;
      } else {
        kheap->chunks = chunk->next;
      }
      chunk->next = NULL;
      // return pointer to user data
      return (void *) chunk + sizeof(chunk_t);
    }
  }

  // kprintf("[kmalloc] creating new chunk\n");

  // if we get this far it means that no existing chunk could
  // fit the requested size therefore a new chunk must be made.
  uintptr_t chunk_start = next_chunk_start();
  // kprintf("[kmalloc] chunk_start: %p\n", chunk_start);
  uintptr_t chunk_end = chunk_start + sizeof(chunk_t) + aligned;
  if (chunk_end > kheap->end_addr) {
    // if we've run out of unclaimed heap space, there is still
    // two more things we can do before signalling an error. First
    // we can try to combine smaller used chunks to form one large
    // enough for the request. If that is unsuccessful, we can
    // (depending on the flags) allocate more pages and expand the
    // total heap size.
    panic("[kmalloc] panic - no available memory\n");
  }

  chunk_t *chunk = (chunk_t *) chunk_start;
  chunk->magic = CHUNK_MAGIC;
  chunk->size = log2(aligned);
  chunk->free = false;
  if (kheap->last_chunk) {
    chunk_t *last = kheap->last_chunk;
    chunk->last.size = last->size;
    chunk->last.free = last->free;
  } else {
    chunk->last.size = 0;
    chunk->last.free = false;
  }

  // kprintf("[kmalloc] new chunk header at %p\n", chunk_start);
  // kprintf("[kmalloc] new chunk data at %p\n", chunk_start + sizeof(chunk_t));

  kheap->last_chunk = chunk;
  return (void *) chunk_start + sizeof(chunk_t);
}

// ----- kfree -----

void kfree(void *ptr) {
  // first verify that the pointer and chunk header are valid
  if (!is_valid_ptr(ptr)) {
    kprintf("[kfree] invalid pointer\n");
    return;
  }

  chunk_t *chunk = get_chunk(ptr);
  chunk_t *next_chunk = get_next_chunk(chunk);
  // kprintf("[kfree] freeing pointer %p\n", ptr);

  // finally mark the chunk as used and add it to the used list
  chunk->free = true;
  chunk->next = kheap->chunks;

  // update the used status in the next chunk
  if (next_chunk) {
    next_chunk->free = true;
  }

  kheap->chunks = chunk;
}

// ----- kcalloc -----

void *kcalloc(size_t nmemb, size_t size) {
   if (nmemb == 0 || size == 0) {
    return NULL;
  }

  // kprintf("kcalloc\n");
  // kprintf("nmemb: %u\n", nmemb);
  // kprintf("size: %u\n", size);
  // kprintf("total: %u\n", nmemb * size);

  size_t total = nmemb * size;
  if (total > CHUNK_MAX_SIZE) {
    // use a different allocator
    kprintf("[kcalloc] request too large\n");
    return NULL;
  }

  void *ptr = kmalloc(total);
  if (ptr) {
    return memset(ptr, 0, total);
  }
  return ptr;
}

// ----- krealloc -----

void *krealloc(void *ptr, size_t size) {
  if (ptr == NULL && size > 0) {
    return kmalloc(size);
  } else if (ptr != NULL && size == 0) {
    kfree(ptr);
    return NULL;
  }

  // verify that ptr is valid
  if (!is_valid_ptr(ptr)) {
    // invalid pointer
    kprintf("[krealloc] invalid pointer\n");
    return NULL;
  }

  size_t aligned = next_pow2(max(size, CHUNK_MIN_SIZE));
  chunk_t *chunk = get_chunk(ptr);
  size_t old_size = 1 << chunk->size;

  // kprintf("krealloc\n");
  // kprintf("ptr: %p\n", ptr);
  // kprintf("size: %u\n", size);
  // kprintf("aligned: %u\n", aligned);
  // kprintf("old size: %u\n", old_size);

  if (aligned <= old_size) {
    // kprintf("[krealloc] no changes needed\n");
    return ptr;
  }

  // start by checking if this chunk was the last one created.
  // if it was, we can try to expand into the unclaimed heap
  // space very easily.
  if (chunk == kheap->last_chunk) {
    uintptr_t chunk_start = (uintptr_t) chunk;
    uintptr_t chunk_end = chunk_start + sizeof(chunk_t) + aligned;
    if (chunk_end < kheap->end_addr) {
      // there is space so all we have to do is change the
      // chunk size and return the same pointer.
      chunk->size = log2(aligned);
      return ptr;
    }
  }

  // then check if the chunk immediately following the current
  // one is available, and if the current size plus the next
  // chunks size would be enough to meet the requested size.
  chunk_t *next_chunk = get_next_chunk(chunk);
  size_t next_size = next_chunk ? 1 << next_chunk->size : 0;
  if (next_chunk && next_chunk->free && old_size + next_size >= aligned) {
    // kprintf("[krealloc] expanding into next chunk\n");

    // find the chunk before this next one in the used list
    chunk_t *curr = kheap->chunks;
    chunk_t *last = NULL;
    while (curr) {
      if (curr == next_chunk) {
        break;
      }

      last = curr;
      curr = curr->next;
    }

    // remove it from the used list
    if (last == NULL) {
      kheap->chunks = next_chunk->next;
    } else {
      last->next = next_chunk->next;
    }

    if (kheap->last_chunk == next_chunk) {
      // if the next chunk was the last chunk created, set it
      // to the current chunk instead. this effectively erases
      // the next_chunk header completely.
      // kprintf("[krealloc] erasing next chunk\n");

      kheap->last_chunk = chunk;
    } else {
      // otherwise we have to move the next chunks header to the
      // end of the next chunk, and then set its magic number to
      // the special HOLE_MAGIC value marking it as a memory 'hole'.
      // these 'holes' are unusable and unreclaimable which is
      // unfortunate but acceptable given how much quicker this
      // method of expansion is compared to relocating the pointer.
      // kprintf("[krealloc] creating memory hole\n");

      next_chunk->magic = HOLE_MAGIC;
      next_chunk->free = false;
      next_chunk->size = 0;

      void *hole_start = next_chunk + next_size;
      memcpy(hole_start, next_chunk, sizeof(chunk_t));
      memset(next_chunk, 0, sizeof(chunk_t));
    }

    // kprintf("[krealloc] expansion complete\n");

    chunk->size = log2(old_size + next_size);
    return ptr;
  }

  // if the above failed, we need to allocate a new block of
  // memory of the correct size and then copy over all of the
  // existing data to that new pointer.
  // kprintf("[krealloc] allocating new chunk\n");

  void *new_ptr = kmalloc(aligned);
  if (new_ptr == NULL) {
    return NULL;
  }

  // kprintf("[krealloc] freeing old chunk\n");
  memcpy(new_ptr, ptr, old_size);
  kfree(ptr);
  return new_ptr;
}