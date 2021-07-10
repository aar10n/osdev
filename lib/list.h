//
// Created by Aaron Gill-Braun on 2021-07-10.
//

#ifndef LIB_LIST_H
#define LIB_LIST_H
#include <base.h>
#include <spinlock.h>

typedef struct list_node {
  struct list_node *next;
  struct list_node *prev;
  void *data;
} list_node_t;

typedef struct list_head {
  list_node_t *first;
  list_node_t *last;
  spinlock_t lock;
} list_head_t;


list_head_t *list_create();
void list_init();
void list_add(list_head_t *list, void *data);
void *list_remove(list_head_t *list, list_node_t *node);

#endif
