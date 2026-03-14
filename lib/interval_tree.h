//
// Created by Aaron Gill-Braun on 2020-10-06.
//

#ifndef LIB_INTERVAL_TREE_H
#define LIB_INTERVAL_TREE_H

#include <interval.h>
#include <rb_tree.h>

//

typedef struct intvl_node {
  rb_node_t *node;
  interval_t interval;
  uint64_t max;
  uint64_t min;
  void *data;
} intvl_node_t;

typedef rb_tree_t intvl_tree_t;


intvl_tree_t *create_intvl_tree();
intvl_node_t *intvl_tree_find(intvl_tree_t *tree, interval_t interval);
void *intvl_tree_get_point(intvl_tree_t *tree, uint64_t point);
interval_t intvl_tree_find_free_gap(intvl_tree_t *tree, interval_t intvl, size_t align, intvl_node_t **prev_node);
void intvl_tree_insert(intvl_tree_t *tree, interval_t interval, void *data);
void intvl_tree_delete(intvl_tree_t *tree, interval_t interval);
void intvl_tree_update_interval(intvl_tree_t *tree, intvl_node_t *node, off_t ds, off_t de);

#endif
