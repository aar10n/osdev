//
// Created by Aaron Gill-Braun on 2020-10-03.
//

#ifndef LIB_BITMAP_H
#define LIB_BITMAP_H

#include <kernel/base.h>

typedef struct bitmap {
  uint64_t *map; // pointer to the actual bitmap
  size_t size;   // size of the bitmap in bytes
  size_t free;   // the number of free bits
  size_t used;   // the number of used bits
} bitmap_t;

typedef ptrdiff_t index_t;

bitmap_t *create_bitmap(size_t n);
bitmap_t *clone_bitmap(bitmap_t *bmp);
void bitmap_init(bitmap_t *bmp, size_t n);
void bitmap_free(bitmap_t *bmp);

bool bitmap_get(bitmap_t *bmp, index_t index);
bool bitmap_set(bitmap_t *bmp, index_t index);
bool bitmap_clear(bitmap_t *bmp, index_t index);
bool bitmap_assign(bitmap_t *bmp, index_t index, bool v);
size_t bitmap_get_n(bitmap_t *bmp, index_t index, size_t n);
size_t bitmap_set_n(bitmap_t *bmp, index_t index, size_t n);
index_t bitmap_get_free(bitmap_t *bmp);
index_t bitmap_get_set_free(bitmap_t *bmp);
index_t bitmap_get_set_free_at(bitmap_t *bmp, index_t index);
index_t bitmap_get_nfree(bitmap_t *bmp, size_t n);
index_t bitmap_get_set_nfree(bitmap_t *bmp, size_t n, size_t align);
ssize_t bitmap_clear_n(bitmap_t *bmp, index_t index, size_t n);

#endif
