//
// Created by Aaron Gill-Braun on 2020-09-02.
//

#ifndef LIB_HASH_TABLE_H
#define LIB_HASH_TABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef INITIAL_MAP_SIZE
#define INITIAL_MAP_SIZE 128
#endif

#ifndef LOAD_FACTOR
#define LOAD_FACTOR 0.75
#endif

// Macros

#define for_in_map(item, map) \
  map_entry_t *item;      \
  for (map_iter_t iter = { map, 0, 0, NULL, NULL, false }; (item = map_next_(&iter));)

// Types

typedef unsigned int (*hasher_t)(char *str);

typedef struct map_entry {
  char *key;
  void *value;
  struct map_entry *next;
} map_entry_t;

typedef struct {
  hasher_t hasher;     // hash function
  size_t size;         // total number of items
  size_t capacity;     // number of buckets
  double load_factor;  // acceptable load factor
  map_entry_t **items; // map entries
} map_base_t;

typedef struct {
  char *key;
  void *value;
} iter_result_base_t;

typedef struct {
  map_base_t *map;
  size_t last_index;
  size_t visited;
  map_entry_t *last_entry;
  iter_result_base_t *result;
  bool done;
} map_iter_t;

// Public API

#define map_t(T) \
  struct { map_base_t map; T *ref; T tmp; }

#define iter_result_t(T) \
  struct { char *key; T *value; }

#define map_init(m) \
  map_init_(&(m)->map)

#define map_free(m) \
  map_free_(&(m)->map)

#define map_get(m, key) \
  ({ (m)->ref = map_get_(&(m)->map, key); (m)->ref ? *(m)->ref : NULL; })

#define map_set(m, key, value) \
  ((m)->tmp = (value), map_set_(&(m)->map, key, &(m)->tmp, sizeof((m)->tmp)))

#define map_delete(m, key) \
  map_delete_(&(m)->map, key)

#define map_iter(m) \
  { &(m)->map, 0, 0, NULL, NULL, false }

#define map_iter_reset(iter) \
  (iter)->last_index = 0;    \
  (iter)->visited = 0;       \
  (iter)->last_entry = NULL; \
  (iter)->result = NULL;     \
  (iter)->done = false;

#define map_next(iter) \
  (map_next_(iter, true) && (iter)->result ? (void *)(iter)->result : NULL)

// Predefined Map Types

typedef map_t(void*) map_void_t;
typedef map_t(char*) map_str_t;
typedef map_t(int) map_int_t;
typedef map_t(char) map_char_t;
typedef map_t(float) map_float_t;
typedef map_t(double) map_double_t;

typedef iter_result_t(void) iter_result_void_t;
typedef iter_result_t(char*) iter_result_str_t;
typedef iter_result_t(int) iter_result_int_t;
typedef iter_result_t(char) iter_result_char_t;
typedef iter_result_t(float) iter_result_float_t;
typedef iter_result_t(double) iter_result_double_t;

// Internal Functions

void map_init_(map_base_t *map);
void map_free_(map_base_t *map);
void *map_get_(map_base_t *map, char *key);
void map_set_(map_base_t *map, char *key, void *value, size_t size);
void map_delete_(map_base_t *map, char *key);
map_entry_t *map_next_(map_iter_t *iter, bool iter_result);

#endif
