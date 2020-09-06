//
// Created by Aaron Gill-Braun on 2020-09-02.
//

#ifndef LIB_HASH_TABLE_H
#define LIB_HASH_TABLE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef unsigned int (*hasher_t)(uint8_t bytes[], size_t len);

typedef struct map_entry {
  unsigned int hash;
  void *value;
  struct map_entry *next;
} map_entry_t;

typedef struct {
  hasher_t hasher;
  size_t size;
  size_t capacity;
  map_entry_t **items;
} map_base_t;

#define map_t(T) \
  struct { map_base_t base; T *ref; T tmp; } // NOLINT(bugprone-macro-parentheses)

#define map_init(m) \
  memset(m, 0, sizeof(*(m)))


typedef map_t(void*) map_void_t;
typedef map_t(char*) map_str_t;
typedef map_t(int) map_int_t;
typedef map_t(char) map_char_t;
typedef map_t(float) map_float_t;
typedef map_t(double) map_double_t;

#endif
