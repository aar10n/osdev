//
// Created by Aaron Gill-Braun on 2022-12-13.
//

#ifndef LIB_HASH_MAP_H
#define LIB_HASH_MAP_H

#include <base.h>
#include <string.h>
#include <murmur3.h>

/// This is a simple header-only implementation of a generic hash map.
/// It is designed to be used only within a compilation unit and not
/// as a type embeded in other types. You can only declare one map type
/// per file.

#ifndef malloc
#include <mm/heap.h>
#define malloc(s) kmalloc(s)
#endif
#ifndef free
#define free(p) kfree(p)
#endif

/**
 * Declare a new map interface for the given type T.
 *
 * The macro declares the following type:
 *     hash_map_t
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
 *     /// returns MAP_ERRVAL.
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
 *     void hash_map_set_c(hash_map_t *map, const char *key, T value);
 *
 *     /// Deletes the entry associated with the given string key and
 *     /// returns the value. If no value is found, it returns MAP_ERRVAL.
 *     T map_delete(hash_map_t *map, const char *key);
 */
#define MAP_TYPE_DECLARE(T) \
  struct __map_item { \
    char *key; \
    T value; \
    struct __map_item *next; \
  }; \
  \
  typedef struct hash_map {     \
    size_t size;                \
    size_t capacity;            \
    struct __map_item **items;  \
  } hash_map_t; \
  \
  static inline hash_map_t *hash_map_new_s(size_t size) { \
    size_t nbytes = sizeof(struct __map_item *) * size; \
    hash_map_t *map = kmalloc(sizeof(hash_map_t)); \
    map->size = 0; \
    map->capacity = size; \
    map->items = malloc(nbytes); \
    memset(map->items, 0, nbytes); \
    return map; \
  } \
  \
  static inline hash_map_t *hash_map_new() { \
    return hash_map_new_s(MAP_DEFAULT_SIZE); \
  } \
  \
  static inline void hash_map_free(hash_map_t *map) { \
    for (size_t i = 0; i < map->capacity; i++) { \
      struct __map_item *item = map->items[i]; \
      while (item != NULL) { \
        struct __map_item *next = item->next; \
        MAP_EVICT_CALLBACK(item->value); \
        free(item->key); \
        free(item); \
        item = next; \
      } \
    } \
   \
    free(map->items); \
    free(map); \
  } \
  \
  static inline T hash_map_get(hash_map_t *map, const char *key) { \
    uint32_t hash = MAP_HASH(key); \
    size_t index = hash % map->capacity; \
   \
    struct __map_item *item = map->items[index]; \
    while (item != NULL) { \
      if (strcmp(item->key, key) == 0) { \
        return item->value; \
      } \
      item = item->next; \
    } \
   \
    return (T)MAP_ERRVAL; \
  } \
  \
  static inline T hash_map_get_d(hash_map_t *map, const char *key, T defval) { \
    T value = hash_map_get(map, key); \
    return value == (T)MAP_ERRVAL ? defval : value; \
  } \
  \
  static inline void hash_map_set(hash_map_t *map, char *key, T value) { \
    uint32_t hash = MAP_HASH(key); \
    size_t index = hash % map->capacity; \
   \
    struct __map_item *item = map->items[index]; \
    while (item != NULL) { \
      if (strcmp(item->key, key) == 0) { \
        MAP_EVICT_CALLBACK(item->value); \
        item->value = value; \
        return; \
      } \
      item = item->next; \
    } \
   \
    item = malloc(sizeof(struct __map_item)); \
    item->key = key; \
    item->value = value; \
    item->next = map->items[index]; \
    map->items[index] = item; \
    map->size++; \
  } \
  \
  static inline void hash_map_set_c(hash_map_t *map, const char *key, T value) { \
    char *key_copy = strdup(key); \
    hash_map_set(map, key_copy, value); \
  } \
  \
  static inline T hash_map_delete(hash_map_t *map, const char *key) { \
    uint32_t hash = MAP_HASH(key); \
    size_t index = hash % map->capacity; \
   \
    struct __map_item *item = map->items[index]; \
    struct __map_item *prev = NULL; \
    while (item != NULL) { \
      if (strcmp(item->key, key) == 0) { \
        if (prev == NULL) { \
          map->items[index] = item->next; \
        } else { \
          prev->next = item->next; \
        } \
        \
        T value = item->value; \
        free(item->key); \
        free(item); \
        map->size--; \
        return value; \
      } \
      prev = item; \
      item = item->next; \
    } \
    \
    return (T)MAP_ERRVAL; \
  }

//
// Configuration Options
//

/// Specifies the default value returned for an error (e.g. key not found).
#ifndef MAP_ERRVAL
#define MAP_ERRVAL {0}
#endif

/// Specifies the function called when a map value is evicted.
#ifndef MAP_EVICT_CALLBACK
#define MAP_EVICT_CALLBACK(v) // do nothing
#endif

/// Specifies the hash function to use. The underlying function
/// should have the prototype:
///     uint32_t (*hash)(const char *key);
#ifndef MAP_HASH
#define MAP_HASH __hash_map_default_hash
static inline uint32_t __hash_map_default_hash(const char *key) {
  size_t len = strlen(key);
  return murmur_hash32(key, len, 0x74747474);
}
#endif

/// Specifies the load threshold at which the map is resized.
#ifndef MAP_LOAD_FACTOR
#define MAP_LOAD_FACTOR 0.75
#endif

/// Specifies the default initial size of a map created with the `hash_map_new()` function.
#ifndef MAP_DEFAULT_SIZE
#define MAP_DEFAULT_SIZE 128
#endif


#endif
