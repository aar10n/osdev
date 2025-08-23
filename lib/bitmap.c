//
// Created by Aaron Gill-Braun on 2020-10-03.
//

#include <bitmap.h>
#include <asm/bits.h>

#include <kernel/panic.h>
#include <kernel/string.h>
#include <kernel/mm/heap.h>

#include <kernel/printf.h>

#ifndef _assert
#define _assert(expr) kassert((expr))
#endif

#ifndef _malloc
#define _malloc(size) kmalloc(size)
#define _free(ptr) kfree(ptr)
#endif

#define MAX_NUM 0xFFFFFFFFFFFFFFFF
#define BIT_SIZE 64
#define BYTE_SIZE 8


bitmap_t *create_bitmap(size_t n) {
  bitmap_t *bmp = _malloc(sizeof(bitmap_t));
  bitmap_init(bmp, n);
  return bmp;
}

bitmap_t *clone_bitmap(bitmap_t *bmp) {
  bitmap_t *clone = _malloc(sizeof(bitmap_t));
  clone->map = _malloc(bmp->size);
  clone->size = bmp->size;
  clone->used = bmp->used;
  clone->free = bmp->free;
  memcpy(clone->map, bmp->map, bmp->size);
  return clone;
}

void bitmap_init(bitmap_t *bmp, size_t n) {
  size_t na = align(n, BIT_SIZE);
  size_t bytes = na / BYTE_SIZE;

  bmp->map = _malloc(bytes);
  bmp->size = bytes;
  bmp->used = 0;
  bmp->free = n;

  memset(bmp->map, 0, bytes);
}

void bitmap_free(bitmap_t *bmp) {
  _free(bmp->map);
  bmp->map = NULL;
  bmp->size = 0;
  bmp->used = 0;
  bmp->free = 0;
}

/**
 * Returns the value of the bit at the specified index.
 */
bool bitmap_get(bitmap_t *bmp, index_t index) {
  _assert(index < bmp->used + bmp->free);
  return __bt64(bmp->map + (index / BIT_SIZE), index % BIT_SIZE);
}

/**
 * Sets the bit at the specified index. Returns `true`
 * if the bit changed or `false` otherwise.
 */
bool bitmap_set(bitmap_t *bmp, index_t index) {
  _assert(index < bmp->used + bmp->free);
  uint8_t result = __bts64(bmp->map + (index / BIT_SIZE), index % BIT_SIZE);
  bmp->used += 1;
  bmp->free -= 1;
  return result;
}

/**
 * Clears the bit at the specified index. Returns `true`
 * if the bit changed or `false` otherwise.
 */
bool bitmap_clear(bitmap_t *bmp, index_t index) {
  _assert(index < bmp->used + bmp->free);
  uint8_t result = __btr64(bmp->map + (index / BIT_SIZE), index % BIT_SIZE);
  bmp->used -= result;
  bmp->free += result;
  return result;
}

/**
 * Assigned value `v` to the bit at the specified index.
 * Returns `true` if the bit changed or `false` otherwise.
 */
bool bitmap_assign(bitmap_t *bmp, index_t index, bool v) {
  _assert(index < bmp->used + bmp->free);
  uint8_t result;
  if (v) {
    uint8_t value = __bts64(bmp->map + (index / BIT_SIZE), index % BIT_SIZE);
    result = (value ^ -v) + v;
  } else {
    uint8_t value = __btr64(bmp->map + (index / BIT_SIZE), index % BIT_SIZE);
    result = (value ^ -v) + v;
  }
  bmp->used -= result;
  bmp->free += result;
  return result;
}

/**
 * Gets the number of 1 bits from the region of n bits starting at the specified index.
 * Returns the total number of 1 bits in the region.
 */
size_t bitmap_get_n(bitmap_t *bmp, index_t index, size_t n) {
  kassert(bmp->free >= n);
  size_t count = 0;
  size_t chunk_count = (n / BIT_SIZE) + (n % BIT_SIZE > 0);
  size_t start_index = index / BIT_SIZE;
  size_t start_bit = index % BIT_SIZE;
  for (size_t i = 0; i < chunk_count; i++) {
    size_t m = min(n, BIT_SIZE);
    uint64_t mask;
    if (i == 0) {
      mask = ((1ULL << m) - 1) << start_bit;
    } else if (i == chunk_count - 1 && n < BIT_SIZE) {
      mask = (1ULL << m) - 1;
    } else {
      mask = MAX_NUM;
    }

    uint64_t value = bmp->map[start_index + i] & mask;
    count += __popcnt64(value);
    n -= m;
  }

  return count;
}

/**
 * Sets the region of n bits starting at the specified index. Returns the
 * number of bits whose value was 1 before the operation.
 */
size_t bitmap_set_n(bitmap_t *bmp, index_t index, size_t n) {
  kassert(bmp->free >= n);
  size_t count = 0;
  size_t chunk_count = (n / BIT_SIZE) + (n % BIT_SIZE > 0);
  size_t start_index = index / BIT_SIZE;
  size_t start_bit = index % BIT_SIZE;
  for (size_t i = 0; i < chunk_count; i++) {
    size_t m = min(n, BIT_SIZE);
    uint64_t mask;
    if (i == 0) {
      mask = ((1ULL << m) - 1) << start_bit;
    } else if (i == chunk_count - 1 && n < BIT_SIZE) {
      mask = (1ULL << m) - 1;
    } else {
      mask = MAX_NUM;
    }

    uint64_t value = bmp->map[start_index + i] & mask;
    count += __popcnt64(value);

    bmp->map[start_index + i] |= mask;
    bmp->used += m;
    bmp->free -= m;
    n -= m;
  }

  return count;
}

/**
 * Returns the index of the first free bit. If there are no
 * free bits, `-1` is returned.
 */
index_t bitmap_get_free(bitmap_t *bmp) {
  if (bmp->free == 0) {
    return -1;
  }

  uint64_t *array = (uint64_t *) bmp->map;
  for (size_t i = 0; i < (bmp->size / BYTE_SIZE); i++) {
    // all bits set
    uint64_t qw = array[i];
    if (qw == MAX_NUM) {
      continue;
    } else if (qw == 0) {
      return i * BIT_SIZE;
    }
    return (i * BIT_SIZE) + __bsf64(~qw);
  }
  return -1;
}

index_t bitmap_get_set_free(bitmap_t *bmp) {
  if (bmp->free == 0) {
    return -1;
  }

  uint64_t *array = (uint64_t *) bmp->map;
  for (size_t i = 0; i < (bmp->size / BYTE_SIZE); i++) {
    uint64_t qw = array[i];
    if (qw == MAX_NUM) {
      // all bits set
      continue;
    } else if (qw == 0) {
      // no bits set
      bmp->map[i] = 1;
      return i * BIT_SIZE;
    }

    size_t offset = __bsf64(~qw);
    bmp->map[i] |= 1ULL << offset;
    return (i * BIT_SIZE) + offset;
  }
  return -1;
}

index_t bitmap_get_set_free_at(bitmap_t *bmp, index_t index) {
  if (bmp->free == 0) {
    return -1;
  }

  size_t total_bits = (bmp->used + bmp->free);
  if (index >= total_bits) {
    return -1;
  }

  uint64_t *array = (uint64_t *) bmp->map;
  size_t start_word = index / BIT_SIZE;
  size_t start_bit = index % BIT_SIZE;
  size_t max_words = (bmp->size / BYTE_SIZE);

  // check the first word starting from the given bit
  if (start_word < max_words) {
    uint64_t qw = array[start_word];
    uint64_t mask = ~((1ULL << start_bit) - 1); // mask to ignore bits before start_bit
    uint64_t masked_qw = qw | ~mask; // set bits before start_bit to 1 (unavailable)
    
    if (masked_qw != MAX_NUM) {
      // there's at least one free bit in this word
      size_t offset = __bsf64(~masked_qw);
      array[start_word] |= (1ULL << offset);
      bmp->used++;
      bmp->free--;
      return (start_word * BIT_SIZE) + offset;
    }
  }

  // check remaining words
  for (size_t i = start_word + 1; i < max_words; i++) {
    uint64_t qw = array[i];
    if (qw == MAX_NUM) {
      // all bits set
      continue;
    } else if (qw == 0) {
      // no bits set
      array[i] |= 1ULL;
      bmp->used++;
      bmp->free--;
      return i * BIT_SIZE;
    }

    size_t offset = __bsf64(~qw);
    array[i] |= (1ULL << offset);
    bmp->used++;
    bmp->free--;
    return (i * BIT_SIZE) + offset;
  }
  
  return -1;
}

/*
 * Returns the start index of the next region of `n` consecutive
 * 0 bits. If no such region could be found, `-1` is returned.
 */
index_t bitmap_get_nfree(bitmap_t *bmp, size_t n) {
  if (n > bmp->free) {
    return -1;
  }

  if (n <= 64) {
    for (size_t i = 0; i < (bmp->size / BYTE_SIZE); i++) {
      // fast case when request is less than 64-bits
      register uint64_t qw = ~(bmp->map[i]);
      if (qw == 0 || __popcnt64(qw) < n) {
        continue;
      } else if (qw == MAX_NUM) {
        // since it was inverted, all bits set to 1 means
        // it was 0 before and all bits are free.
        bmp->free -= n;
        return i * BIT_SIZE;
      }

      uint8_t p = 0;
      uint8_t k;
      while (qw > 0) {
        // skip past zeros
        k = __bsf64(qw);
        qw >>= k;
        p += k;
        k = __bsf64(~qw);
        if (k >= n) {
          bmp->free -= n;
          return (i * BIT_SIZE) + p;
        }
        // skip past ones
        qw >>= k;
        p += k;
      }
    }
  } else {
    // slow case when request is larger than 64-bits
    // find the start index of enough consecutive 0's
    // to satisfy the requested count
    size_t max_index = bmp->size / BYTE_SIZE;
    size_t chunk_count = (n / BIT_SIZE) + (n % BIT_SIZE > 0);
    index_t index = -1;
    for (size_t i = 0; i < max_index; i++) {
      if (bmp->map[i] != 0) {
        continue;
      }

      size_t found = 0;
      for (size_t j = i; j < max_index; j++) {
        if (bmp->map[j] != 0) {
          i = j + 1;
          goto outer; // keep searching
        }

        found++;
        if (found >= chunk_count) {
          break;
        }
      }

      if (found < chunk_count) {
        // the end of the map was reached but not enough
        // free space was found
        return -1;
      }
      index = i;
      break;
    outer:;
    }

    return index * BIT_SIZE;
  }
  return -1;
}

/*
 * Returns the start index of the next region of `n` consecutive
 * 0 bits and sets them to 1. If no such region could be found,
 * `-1` is returned.
 */
index_t bitmap_get_set_nfree(bitmap_t *bmp, size_t n, size_t align) {
  if (n > bmp->free) {
    return -1;
  } else if ((align & (align - 1)) != 0) {
    // align must be a power of two
    return -1;
  }

  if (n <= 64) {
    kassert(align == 0);
    // fast case when request is less than 64-bits
    for (size_t i = 0; i < (bmp->size / BYTE_SIZE); i++) {
      register uint64_t qw = ~(bmp->map[i]);
      if (qw == 0 || __popcnt64(qw) < n) {
        continue;
      } else if (qw == MAX_NUM) {
        // since it was inverted, all bits set to 1 means
        // it was 0 before and all bits are free.
        bmp->free -= n;
        bmp->map[i] |= (MAX_NUM >> (BIT_SIZE - n));
        return i * BIT_SIZE;
      }

      uint8_t p = 0;
      uint8_t k;
      while (qw > 0) {
        // skip past zeros
        k = __bsf64(qw);
        qw >>= k;
        p += k;
        k = __bsf64(~qw);
        if (k >= n) {
          bmp->used += n;
          bmp->free -= n;
          bmp->map[i] |= (MAX_NUM >> (BIT_SIZE - n)) << p;
          return (i * BIT_SIZE) + p;
        }
        // skip past ones
        qw >>= k;
        p += k;
      }
    }
  } else {
    if (align == 0) {
      align = 1;
    }

    // slow case when request is larger than 64-bits
    // find the start index of enough consecutive 0's
    // to satisfy the requested count
    size_t max_index = bmp->size / BYTE_SIZE;
    size_t chunk_count = (n / BIT_SIZE) + (n % BIT_SIZE > 0);
    index_t index = -1;
    for (size_t i = 0; i < max_index; i++) {
      if (bmp->map[i] != 0 || (i * BIT_SIZE) % align != 0) {
        continue;
      }

      size_t found = 0;
      for (size_t j = i; j < max_index; j++) {
        if (bmp->map[j] != 0) {
          i = j + 1;
          goto outer; // keep searching
        }

        found++;
        if (found >= chunk_count) {
          break;
        }
      }

      if (found < chunk_count) {
        // the end of the map was reached but not enough
        // free space was found
        return -1;
      }
      index = i;
      break;
    outer:;
    }

    size_t remaining = n;
    for (size_t i = 0; i < chunk_count; i++) {
      size_t v = min(remaining, BIT_SIZE);
      remaining -= v;
      if (remaining > 0) {
        bmp->map[index + i] = MAX_NUM;
      } else {
        // last index
        if (v == 64) {
          bmp->map[index + i] = MAX_NUM;
        } else {
          bmp->map[index + i] = (MAX_NUM >> (BIT_SIZE - remaining));
        }
        bmp->used += n;
        bmp->free -= n;
        return index * BIT_SIZE;
      }
    }
    unreachable;
  }
  return -1;
}

/*
 * Clears a region of `n` bits.
 */
ssize_t bitmap_clear_n(bitmap_t *bmp, index_t index, size_t n) {
  kassert(bmp->used >= n);
  ssize_t cleared = n;
  size_t chunk_count = (n / BIT_SIZE) + (n % BIT_SIZE > 0);
  size_t start_index = index / BIT_SIZE;
  size_t start_bit = index % BIT_SIZE;
  for (size_t i = 0; i < chunk_count; i++) {
    size_t m = min(n, BIT_SIZE - (i == 0 ? start_bit : 0));
    uint64_t mask;
    if (i == 0) {
      mask = ((1ULL << m) - 1) << start_bit;
    } else if (i == chunk_count - 1 && n < BIT_SIZE) {
      mask = (1ULL << m) - 1;
    } else {
      mask = MAX_NUM;
    }

    bmp->map[start_index + i] &= ~mask;
    bmp->used -= m;
    bmp->free += m;
    n -= m;
  }
  return cleared;
}
