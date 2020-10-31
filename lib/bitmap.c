//
// Created by Aaron Gill-Braun on 2020-10-03.
//

#include "bitmap.h"
#include "asm/bitmap.h"

#include <panic.h>
#include <stdio.h>

#ifndef _assert
#define _assert(expr) kassert((expr))
#endif

#define MAX_NUM 0xFFFFFFFFFFFFFFFF
#define BIT_SIZE 64
#define BYTE_SIZE 8


/**
 * Returns the value of the bit at the specified index.
 */
bool bitmap_get(bitmap_t *bmp, index_t index) {
  _assert(index < bmp->used + bmp->free);
  return __bt8(bmp->map[index / BIT_SIZE], index % BIT_SIZE);
}

/**
 * Sets the bit at the specified index. Returns `true`
 * if the bit changed or `false` otherwise.
 */
bool bitmap_set(bitmap_t *bmp, index_t index) {
  _assert(index < bmp->used + bmp->free);
  uint8_t result = __bts64(bmp->map + (index / BIT_SIZE), index % BIT_SIZE);
  bmp->used += result;
  bmp->free -= result;
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
      return i;
    }
    return (i * BIT_SIZE) + __bsf64(~qw);
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

  if (n < 64) {
    for (size_t i = 0; i < (bmp->size / BYTE_SIZE); i++) {
      // fast case when request is less than 64-bits
      register uint64_t qw = ~(bmp->map[i]);
      if (qw == 0 || __popcnt64(qw) < n) {
        continue;
      } else if (qw == MAX_NUM) {
        // since it was inverted, all bits set to 1 means
        // it was 0 before and all bits are free.
        bmp->free -= n;
        return i;
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
    // slow case when the request is larger than 64-bits
    // in this case we always align the requests to the
    // start of a qword.
    index_t index = -1;
    size_t remaining = n;
    for (size_t i = 0; i < (bmp->size / BYTE_SIZE); i++) {
      if (bmp->map[i] == 0) {
        if (index == -1) {
          // mark start of region
          index = i;
        }
        remaining -= BIT_SIZE;
      } else if (remaining < BIT_SIZE && __popcnt64(bmp->map[i]) >= remaining) {
        // still valid
        remaining = 0;
      } else {
        // reset index and count
        index = -1;
        remaining -= BIT_SIZE;
      }

      if (remaining == 0) {
        break;
      }
    }

    if (n != -1) {
      bmp->free -= n;
    }
    return index;
  }
  return -1;
}

/*
 * Returns the start index of the next region of `n` consecutive
 * 0 bits and sets them to 1. If no such region could be found,
 * `-1` is returned.
 */
index_t bitmap_get_set_nfree(bitmap_t *bmp, size_t n) {
  if (n > bmp->free) {
    return -1;
  }

  if (n < 64) {
    // fast case when request is less than 64-bits
    for (size_t i = 0; i < (bmp->size / BYTE_SIZE); i++) {
      register uint64_t qw = ~(bmp->map[i]);
      if (qw == 0 || __popcnt64(qw) < n) {
        continue;
      } else if (qw == MAX_NUM) {
        // since it was inverted, all bits set to 1 means
        // it was 0 before and all bits are free.
        bmp->free -= n;
        return i;
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
          bmp->map[i] |= (MAX_NUM >> (BIT_SIZE - n)) << p;
          return (i * BIT_SIZE) + p;
        }
        // skip past ones
        qw >>= k;
        p += k;
      }
    }
  } else {
    // slow case when request is larger than 64-bits
    index_t region = bitmap_get_nfree(bmp, n);
    if (region == -1) {
      return -1;
    }

    index_t index = region;
    size_t remaining = n;
    size_t chunk_count = (n / BIT_SIZE) + (n % BIT_SIZE > 0);
    for (size_t i = 0; i < chunk_count; i++) {
      if (i < chunk_count - 1) {
        bmp->map[index + i] = MAX_NUM;
        remaining -= BIT_SIZE;
      } else {
        // last index
        bmp->map[index + i] = (0xFFFFFFFFFFFFFFFF >> (BIT_SIZE - remaining));
        return index;
      }
    }
  }
  return -1;
}

/*
 * Clears a region of `n` bits.
 */
ssize_t bitmap_clear_n(bitmap_t *bmp, index_t index, size_t n) {
  ssize_t cleared = 0;
  size_t remaining = n;
  size_t chunk_count = (n / BIT_SIZE) + (n % BIT_SIZE > 0);
  for (size_t i = 0; i < chunk_count; i++) {
    if (i < chunk_count - 1) {
      bmp->map[index + i] = 0;

      remaining -= BIT_SIZE;
      cleared += BIT_SIZE;
    } else {
      // last index
      bmp->map[index + i] ^= (0xFFFFFFFFFFFFFFFF >> (BIT_SIZE - remaining));
      return cleared + remaining;
    }
  }
  return cleared;
}
