//
// Created by Aaron Gill-Braun on 2020-10-06.
//

#ifndef LIB_INTERVAL_TREE_H
#define LIB_INTERVAL_TREE_H

#include <base.h>
#include <rb_tree.h>

#define intvl(start, end) \
  ((interval_t){ start, end })

typedef struct interval {
  uint64_t start;
  uint64_t end;
} interval_t;

typedef struct {
  interval_t interval;
  uint64_t max;
  void *data;
} intvl_node_t;

typedef struct {
  rb_tree_t *tree;
} intvl_tree_t;

typedef rb_iter_t intvl_iter_t;

intvl_tree_t *create_intvl_tree();
intvl_node_t *intvl_tree_search(intvl_tree_t *tree, interval_t interval);
void intvl_tree_insert(intvl_tree_t *tree, interval_t interval, void *data);
void intvl_tree_delete(intvl_tree_t *tree, interval_t interval);

intvl_iter_t *intvl_iter_tree(intvl_tree_t *tree);
intvl_node_t *intvl_iter_next(intvl_iter_t *iter);

#endif
