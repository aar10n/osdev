//
// Created by Aaron Gill-Braun on 2020-10-12.
//

#ifndef LIB_RB_TREE_H
#define LIB_RB_TREE_H

#include <base.h>

typedef enum {
  RED, BLACK
} rb_color_t;

typedef struct rb_node {
  uint64_t key;
  void *data;
  rb_color_t color;
  struct rb_node *left;
  struct rb_node *right;
  struct rb_node *parent;
} rb_node_t;

typedef struct {
  rb_node_t *root;
  rb_node_t *nil;
} rb_tree_t;

rb_tree_t *create_rb_tree();
rb_node_t *rb_tree_search(rb_tree_t *tree, uint64_t key);
void rb_tree_insert(rb_tree_t *tree, uint64_t key, void *data);
void rb_tree_delete(rb_tree_t *tree, uint64_t key);

#endif
