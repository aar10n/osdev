//
// Created by Aaron Gill-Braun on 2020-10-12.
//

#ifndef LIB_RB_TREE_H
#define LIB_RB_TREE_H

#include <kernel/base.h>

struct rb_tree;
struct rb_node;
struct rb_tree_events;

enum rb_color {
  RED,
  BLACK
};

typedef struct rb_tree {
  struct rb_node *root;
  struct rb_node *nil;
  struct rb_node *min;
  struct rb_node *max;
  size_t nodes;

  struct rb_tree_events *events;
} rb_tree_t;

typedef struct rb_node {
  uint64_t key;
  void *data;
  enum rb_color color;
  struct rb_node *left;
  struct rb_node *right;
  struct rb_node *parent;
  struct rb_node *next;
  struct rb_node *prev;
} rb_node_t;

typedef void *(*rb_copy_data_t)(rb_tree_t *otree, rb_node_t *onode);

// event callbacks
typedef void (*rb_evt_pre_rotate_t)(rb_tree_t *tree, rb_node_t *x, rb_node_t *y);
typedef void (*rb_evt_post_rotate_t)(rb_tree_t *tree, rb_node_t *x, rb_node_t *y);
typedef void (*rb_evt_pre_insert_node_t)(rb_tree_t *tree, rb_node_t *z);
typedef void (*rb_evt_post_insert_node_t)(rb_tree_t *tree, rb_node_t *z);
typedef void (*rb_evt_pre_delete_node_t)(rb_tree_t *tree, rb_node_t *z);
typedef void (*rb_evt_post_delete_node_t)(rb_tree_t *tree, rb_node_t *z, rb_node_t *x);
typedef void (*rb_evt_replace_node_t)(rb_tree_t *tree, rb_node_t *u, rb_node_t *v);
typedef void (*rb_evt_dup_node_t)(rb_tree_t *otree, rb_tree_t *ntree, rb_node_t *u, rb_node_t *v);

typedef struct rb_tree_events {
  rb_evt_pre_rotate_t pre_rotate;
  rb_evt_post_rotate_t post_rotate;
  rb_evt_pre_insert_node_t pre_insert_node;
  rb_evt_post_insert_node_t post_insert_node;
  rb_evt_pre_delete_node_t pre_delete_node;
  rb_evt_post_delete_node_t post_delete_node;
  rb_evt_replace_node_t replace_node;
  rb_evt_dup_node_t duplicate_node;
} rb_tree_events_t;


rb_tree_t *create_rb_tree();
void rb_tree_free(rb_tree_t *tree);
rb_tree_t *copy_rb_tree(rb_tree_t *tree);

rb_node_t *rb_tree_find_node(rb_tree_t *tree, uint64_t key);
void rb_tree_insert_node(rb_tree_t *tree, rb_node_t *node);
void *rb_tree_delete_node(rb_tree_t *tree, rb_node_t *node);

void *rb_tree_find(rb_tree_t *tree, uint64_t key);
void rb_tree_insert(rb_tree_t *tree, uint64_t key, void *data);
void *rb_tree_delete(rb_tree_t *tree, uint64_t key);


static inline bool rb_node_is_nil(rb_node_t *node) {
  return node->data == NULL && node == node->parent;
}

static inline rb_node_t *rb_node_next(rb_node_t *node) {
  if (rb_node_is_nil(node->next)) {
    return NULL;
  }
  return node->next;
}

static inline rb_node_t *rb_node_prev(rb_node_t *node) {
  if (rb_node_is_nil(node->prev)) {
    return NULL;
  }
  return node->prev;
}

#endif
