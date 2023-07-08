//
// Created by Aaron Gill-Braun on 2022-12-13.
//

#ifndef LIB_HASH_MAP_H
#define LIB_HASH_MAP_H

#include <kernel/base.h>
#include <kernel/string.h>
#include <kernel/str.h>

/// This is a simple header-only implementation of a generic hash map.
/// It is designed to be used only within a single file and not as a
/// type embeded within other types. You can only have one unique map
/// type per compilation unit.

#ifndef HMAP_TYPE
#error "HMAP_TYPE must be defined"
#endif

#define T HMAP_TYPE

// Configuration Options

/// Specifies the default value returned for an error (e.g. key not found).
#ifndef HMAP_ERRVAL
#define HMAP_ERRVAL {0}
#endif

/// Specifies the function called when a map value is evicted.
#ifndef HMAP_EVICT_CALLBACK
#define HMAP_EVICT_CALLBACK(v) // do nothing
#endif

/// Specifies the hash function to use. The underlying function
/// should have the prototype:
///     uint32_t (*hash)(const char *key, size_t len);
#ifndef HMAP_HASH
#define HMAP_HASH __hash_map_default_hash
#include <murmur3.h>
static inline uint32_t __hash_map_default_hash(const char *key, size_t len) {
  return murmur_hash32(key, (int) len, 0x74747474);
}
#endif

/// Specifies the load threshold at which the map is resized.
#ifndef HMAP_LOAD_FACTOR
#define HMAP_LOAD_FACTOR 0.75
#endif

/// Specifies the default initial size of a map created with the `hash_map_new()` function.
#ifndef HMAP_DEFAULT_SIZE
#define HMAP_DEFAULT_SIZE 128
#endif

#ifndef HMAP_ALLOC
#include <kernel/mm/heap.h>
#define HMAP_ALLOC(s) kmalloc(s)
#endif
#ifndef HMAP_FREE
#define HMAP_FREE(p) kfree(p)
#endif

/**
 * Declare a new map interface for the given type T.
 *
 * The macro declares the following type:
 *     hash_map_t
 *
 * The macro declares the following functions:
 *
 *     /// Allocates a new hash map with the default initial size.
 *     hash_map_t *hash_map_new();
 *
 *     /// Allocates a new hash map with the given initial size.
 *     hash_map_t *hash_map_new_s(size_t size);
 *
 *     /// Deallocates the given hash map and all of its remaining items.
 *     void hash_map_free(hash_map_t *map);
 *
 *     /// Looks up the value for the given key. If no value is found, it
 *     /// returns HMAP_ERRVAL.
 *     T hash_map_get(hash_map_t *map, const char *key);
 *
 *     /// Looks up the value for the given key. If no value is found, it
 *     /// returns the provided default value.
 *     T hash_map_get_d(hash_map_t *map, const char *key, T defval);
 *
 *     /// Sets the given string key to the provided value. The string
 *     /// ownership transfers to the map.
 *     void hash_map_set(hash_map_t *map, char *key, T value);
 *
 *     /// Sets the given string key to the provided value. The string
 *     /// is copied and the map does not take ownership.
 *     void hash_map_set(hash_map_t *map, const char *key, T value);
 *
 *     /// Deletes the entry associated with the given string key and
 *     /// returns the value. If no value is found, it returns HMAP_ERRVAL.
 *     T map_delete(hash_map_t *map, const char *key);
 */

struct __map_item {
  char *key;
  size_t key_len;
  T value;
  struct __map_item *next;
};

typedef struct hash_map {
  size_t size;
  size_t capacity;
  struct __map_item **items;
} hash_map_t;

static inline hash_map_t *hash_map_new_s(size_t size) {
  size_t nbytes = sizeof(struct __map_item *) * size;
  hash_map_t *map = kmalloc(sizeof(hash_map_t));
  map->size = 0;
  map->capacity = size;
  map->items = HMAP_ALLOC(nbytes);
  memset(map->items, 0, nbytes);
  return map;
}

static inline hash_map_t *hash_map_new() {
  return hash_map_new_s(HMAP_DEFAULT_SIZE);
}

static inline void hash_map_free(hash_map_t *map) {
  for (size_t i = 0; i < map->capacity; i++) {
    struct __map_item *item = map->items[i];
    while (item != NULL) {
      struct __map_item *next = item->next;
      HMAP_EVICT_CALLBACK(item->value);
      HMAP_FREE(item->key);
      HMAP_FREE(item);
      item = next;
    }
  }

  HMAP_FREE(map->items);
  HMAP_FREE(map);
}

static inline T __hash_map_get(hash_map_t *map, const char *key, size_t len) {
  uint32_t hash = HMAP_HASH(key, len);
  size_t index = hash % map->capacity;

  struct __map_item *item = map->items[index];
  while (item != NULL) {
    if (len == item->key_len && strncmp(key, item->key, len) == 0) {
      return item->value;
    }
    item = item->next;
  }

  return (T)HMAP_ERRVAL;
}

static inline T hash_map_get(hash_map_t *map, const char *key) {
  return __hash_map_get(map, key, strlen(key));
}

static inline T hash_map_get_cstr(hash_map_t *map, cstr_t key) {
  return __hash_map_get(map, cstr_ptr(key), cstr_len(key));
}

static inline void __hash_map_set(hash_map_t *map, char *key, size_t len, T value) {
  uint32_t hash = HMAP_HASH(key, len);
  size_t index = hash % map->capacity;

  struct __map_item *item = map->items[index];
  while (item != NULL) {
    if (len == item->key_len && strncmp(key, item->key, len) == 0) {
      HMAP_EVICT_CALLBACK(item->value);
      item->value = value;
      HMAP_FREE(key);
      return;
    }
    item = item->next;
  }

  item = HMAP_ALLOC(sizeof(struct __map_item));
  item->key = key;
  item->key_len = len;
  item->value = value;
  item->next = map->items[index];
  map->items[index] = item;
  map->size++;
}

static inline void hash_map_set_str(hash_map_t *map, str_t key, T value) {
  __hash_map_set(map, str_mut_ptr(key), str_len(key), value);
}

static inline void hash_map_set(hash_map_t *map, const char *key, T value) {
  size_t len = strlen(key);
  char *key_copy = HMAP_ALLOC(len + 1);
  memcpy(key_copy, key, len);
  key_copy[len] = '\0';
  __hash_map_set(map, key_copy, len, value);
}

static inline T __hash_map_delete(hash_map_t *map, const char *key, size_t len) {
  uint32_t hash = HMAP_HASH(key, len);
  size_t index = hash % map->capacity;

  struct __map_item *item = map->items[index];
  struct __map_item *prev = NULL;
  while (item != NULL) {
    if (len == item->key_len && strncmp(key, item->key, len) == 0) {
      if (prev == NULL) {
        map->items[index] = item->next;
      } else {
        prev->next = item->next;
      }

      T value = item->value;
      HMAP_FREE(item->key);
      HMAP_FREE(item);
      map->size--;
      return value;
    }
    prev = item;
    item = item->next;
  }

  return (T)HMAP_ERRVAL;
}

static inline T hash_map_delete(hash_map_t *map, const char *key) {
  return __hash_map_delete(map, key, strlen(key));
}

static inline T hash_map_delete_cstr(hash_map_t *map, cstr_t key) {
  return __hash_map_delete(map, cstr_ptr(key), cstr_len(key));
}

#undef T
#undef HMAP_ALLOC
#undef HMAP_FREE
#undef HMAP_LOAD_FACTOR
#undef HMAP_DEFAULT_SIZE

#endif
