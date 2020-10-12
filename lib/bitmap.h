//
// Created by Aaron Gill-Braun on 2020-10-03.
//

#ifndef LIB_BITMAP_H
#define LIB_BITMAP_H

#include <base.h>

typedef struct {
  uint64_t *map; // pointer to the actual bitmap
  size_t size;   // size of the bitmap in bytes
  size_t free;   // the number of free bits
  size_t used;   // the number of used bits
} bitmap_t;

typedef ptrdiff_t index_t;

bool bitmap_get(bitmap_t *bmp, index_t index);
bool bitmap_set(bitmap_t *bmp, index_t index);
bool bitmap_clear(bitmap_t *bmp, index_t index);
bool bitmap_assign(bitmap_t *bmp, index_t index, bool v);
index_t bitmap_get_free(bitmap_t *bmp);
index_t bitmap_get_nfree(bitmap_t *bmp, uint8_t n);
index_t bitmap_get_set_nfree(bitmap_t *bmp, uint8_t n);

#endif