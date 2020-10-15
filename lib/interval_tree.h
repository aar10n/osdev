//
// Created by Aaron Gill-Braun on 2020-10-06.
//

#ifndef LIB_INTERVAL_TREE_H
#define LIB_INTERVAL_TREE_H

#include <base.h>
#include <rb_tree.h>

#define intvl(start, end) \
  ((interval_t){ start, end })

#define NULL_SET ((interval_t){ UINT64_MAX, 0 })

#define is_null_set(i) \
  ((i).start == UINT64_MAX && (i).end == 0)

#define intersection(i, j) \
  (((j).start > (i).end) || ((i).start > (j).end) ? \
    NULL_SET : intvl(max(i.start, j.start), min(i.end, j.end)))

#define overlaps(i, j) \
  (!is_null_set(intersection(i, j)))


typedef struct interval {
  uint64_t start;
  uint64_t end;
} interval_t;

typedef struct {
  rb_node_t *node;
  interval_t interval;
  uint64_t max;
  uint64_t min;
  void *data;
} intvl_node_t;

typedef struct {
  rb_tree_t *tree;
} intvl_tree_t;

typedef rb_iter_t intvl_iter_t;

intvl_tree_t *create_intvl_tree();
intvl_node_t *intvl_tree_find(intvl_tree_t *tree, interval_t interval);
intvl_node_t *intvl_tree_find_closest(intvl_tree_t *tree, interval_t interval);
void intvl_tree_insert(intvl_tree_t *tree, interval_t interval, void *data);
void intvl_tree_delete(intvl_tree_t *tree, interval_t interval);

intvl_iter_t *intvl_iter_tree(intvl_tree_t *tree);
intvl_node_t *intvl_iter_next(intvl_iter_t *iter);

#endif
