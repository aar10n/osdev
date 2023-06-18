//
// Created by Aaron Gill-Braun on 2020-09-02.
//

#include "hash_table.h"

#include <kernel/printf.h>

#ifndef _malloc
#include <kernel/mm.h>
#define _malloc(size) kmalloc(size)
#define _free(ptr) kfree(ptr)
#endif

#define for_in(item, map) \
  map_entry_t *item;      \
  for (map_iter_t iter = { map, 0, 0, NULL, NULL, false }; (item = map_next_(&iter, false));)

//

static unsigned default_hasher(char *str) {
  unsigned hash = 5381;
  while (*str) {
    hash = ((hash << 5) + hash) ^ *str++;
  }
  return hash;
}

//

void map_init_(map_base_t *map) {
  memset(map, 0, sizeof(map_base_t));
  !map->hasher && (map->hasher = default_hasher);
  !map->capacity && (map->capacity = INITIAL_MAP_SIZE);
  !map->load_factor && (map->load_factor = LOAD_FACTOR);
  map->items = _malloc(map->capacity * sizeof(map_entry_t *));
  memset(map->items, 0, map->capacity * sizeof(map_entry_t *));
  map->size = 0;
}

void map_free_(map_base_t *map) {
  for (size_t i = 0; i < map->capacity; i++) {
    map_entry_t *entry = map->items[i];

    map_entry_t *next;
    while (entry) {
      next = entry->next;
      _free(entry->key);
      _free(entry->value);
      _free(entry);
      entry = next;
    }
  }

  _free(map->items);
}

//

void map_resize_(map_base_t *map, size_t new_size) {
  map_entry_t **new_items = _malloc(new_size * sizeof(map_entry_t *));

  for_in(entry, map) {
    unsigned hash = map->hasher(entry->key);
    unsigned index = hash % new_size;

    if (new_items[index]) {
      map_entry_t *last = new_items[index];
      while (last->next) {
        last = last->next;
      }

      last->next = entry;
    } else {
      new_items[index] = entry;
    }
  }

  _free(map->items);
  map->items = new_items;
  map->capacity = new_size;
}

void *map_get_(map_base_t *map, char *key) {
  unsigned hash = map->hasher(key);
  unsigned index = hash % map->capacity;

  map_entry_t *item = map->items[index];
  if (!item) {
    return NULL;
  }

  while (item) {
    unsigned item_hash = map->hasher(item->key);
    if (item_hash == hash) {
      return item->value;
    }
    item = item->next;
  }

  return NULL;
}

void map_set_(map_base_t *map, char *key, void *value, size_t size) {
  double lf = (double) (map->size + 1) / map->capacity;
  if (lf > LOAD_FACTOR) {
    map_resize_(map, map->capacity * 2);
  }

  unsigned hash = map->hasher(key);
  unsigned index = hash % map->capacity;

  size_t key_size = strlen(key) + 1;

  map_entry_t *entry = _malloc(sizeof(map_entry_t));

  char *tmp_key = _malloc(key_size);
  void *tmp_value = _malloc(size);
  memcpy(tmp_key, key, key_size);
  memcpy(tmp_value, value, size);

  entry->key = tmp_key;
  entry->value = tmp_value;

  if (map->items[index]) {
    kprintf("[hash_table] collision\n");
    map_entry_t *curr = map->items[index];
    while (curr) {
      unsigned curr_hash = map->hasher(curr->key);
      if (curr_hash == hash) {
        _free(curr->key);
        _free(curr->value);
        _free(entry);
        curr->value = tmp_key;
        curr->value = tmp_value;
        return;
      }

      if (!curr->next) break;
      curr = curr->next;
    }

    curr->next = entry;
    return;
  } else {
    map->items[index] = entry;
  }

  map->size++;
}

void map_delete_(map_base_t *map, char *key) {
  unsigned hash = map->hasher(key);
  unsigned index = hash % map->capacity;

  if (map->items[index]) {
    map_entry_t *entry = map->items[index];
    unsigned entry_hash = map->hasher(entry->key);
    if (entry_hash == hash) {
      map->items[index] = entry->next;
      _free(entry->key);
      _free(entry->value);
      _free(entry);
      map->size--;
      return;
    }

    map_entry_t *last;
    map_entry_t *curr = entry;
    while (curr) {
      unsigned curr_hash = map->hasher(curr->key);
      if (curr_hash == hash) {
        last->next = curr->next;
        _free(curr->key);
        _free(curr->value);
        _free(curr);
        map->size--;
        return;
      }

      last = curr;
      curr = curr->next;
    }
  }
}

//

map_entry_t *map_next_(map_iter_t *iter, bool iter_result) {
  if (iter->result) {
    _free(iter->result);
    iter->result = NULL;
  }

  if (iter->done || iter->visited >= iter->map->size) {
    iter->done = true;
    iter->result = NULL;
    return NULL;
  }

  if (iter->last_entry) {
    if (iter->last_entry->next) {
      map_entry_t *next = iter->last_entry->next;
      iter->visited++;
      iter->last_entry = next;

      if (iter_result) {
        iter_result_base_t *result = _malloc(sizeof(iter_result_base_t));
        result->key = next->key;
        result->value = next->value;

        iter->result = result;
      }

      return next;
    }

    iter->last_index++;
  }

  for (size_t i = iter->last_index; i < iter->map->capacity; i++) {
    map_entry_t *entry = iter->map->items[i];
    if (entry) {
      iter->visited++;
      iter->last_entry = entry;

      if (iter_result) {
        iter_result_base_t *result = _malloc(sizeof(iter_result_base_t));
        result->key = entry->key;
        result->value = entry->value;

        iter->result = result;
      }
      return entry;
    }

    iter->last_index++;
  }

  iter->done = true;
  iter->result = NULL;
  return NULL;
}
