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
index_t bitmap_get_nfree(bitmap_t *bmp, uint8_t n) {
  _assert(n <= 64);
  if (n > bmp->free) {
    return -1;
  }

  for (size_t i = 0; i < (bmp->size / BYTE_SIZE); i++) {
    register uint64_t qw = ~(bmp->map[i]);
    if (qw == 0 || __popcnt64(qw) < n) {
      continue;
    } else if (qw == MAX_NUM) {
      // since it was inverted, all bits set to 1 means
      // it was 0 before and all bits are free.
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
        return (i * BIT_SIZE) + p;
      }
      // skip past ones
      qw >>= k;
      p += k;
    }
  }
  return -1;
}

/*
 * Returns the start index of the next region of `n` consecutive
 * 0 bits and sets them to 1. If no such region could be found,
 * `-1` is returned.
 */
index_t bitmap_get_set_nfree(bitmap_t *bmp, uint8_t n) {
  _assert(n <= 64);
  if (n > bmp->free) {
    return -1;
  }

  for (size_t i = 0; i < (bmp->size / BYTE_SIZE); i++) {
    register uint64_t qw = ~(bmp->map[i]);
    if (qw == 0 || __popcnt64(qw) < n) {
      continue;
    } else if (qw == MAX_NUM) {
      // since it was inverted, all bits set to 1 means
      // it was 0 before and all bits are free.
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
        bmp->map[i] |= (MAX_NUM >> (BIT_SIZE - n)) << p;
        return (i * BIT_SIZE) + p;
      }
      // skip past ones
      qw >>= k;
      p += k;
    }
  }
  return -1;
}
