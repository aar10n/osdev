//
// Created by Aaron Gill-Braun on 2020-10-06.
//

#ifndef LIB_INTERVAL_TREE_H
#define LIB_INTERVAL_TREE_H

#include <base.h>

#define IS_ERROR_INTERVAL(i) \
  ((i).start == UINT64_MAX && (i).end == 0)

#define ERROR_INTERVAL \
  ((interval_t){ .start = UINT64_MAX, .end = 0 })


typedef struct interval {
  uint64_t start;
  uint64_t end;
} interval_t;

typedef struct intvl_node {
  interval_t interval;
  bool is_child;
  void *data;

  struct intvl_node *left;
  struct intvl_node *right;
  struct intvl_node *parent;
} intvl_node_t;

intvl_node_t *create_interval_tree(uint64_t min, uint64_t max);
bool tree_add_interval(intvl_node_t *tree, interval_t interval, void *data);
bool tree_remove_interval(intvl_node_t *tree, interval_t interval);
interval_t tree_find_free_interval(intvl_node_t *tree, size_t min_size);
intvl_node_t *tree_query_point(intvl_node_t *tree, uint64_t point);
intvl_node_t *tree_query_interval(intvl_node_t *tree, uint64_t start, uint64_t end);

#endif
