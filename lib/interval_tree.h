//
// Created by Aaron Gill-Braun on 2020-10-06.
//

#ifndef LIB_INTERVAL_TREE_H
#define LIB_INTERVAL_TREE_H

#include <kernel/base.h>
#include <rb_tree.h>

#define NULL_SET ((interval_t){ UINT64_MAX, 0 })

#define intvl(start, end) \
  ((interval_t){ start, end })

#define magnitude(i) \
  ((i).end - (i).start)

#define is_null_set(i) \
  ((i).start == UINT64_MAX && (i).end == 0)

#define intvl_eq(i, j) \
  (((i).start == (j).start) && ((i).end == (j).end))

// intersection of i and j
#define intersection(i, j) \
  (((j).start >= (i).end) || ((i).start >= (j).end) ? \
    NULL_SET : intvl(max((i).start, (j).start), min((i).end, (j).end)))

// subtract j from i (i - j)
#define subtract(i, j) \
  ((contains(i, j) || !overlaps(i, j)) ? NULL_SET : (i).start < (j).start ? \
    intvl((i).start, (j).start) : \
    intvl((j).end, (i).end))

// i and j are contiguous
#define contiguous(i, j) \
  (!overlaps(i, j) && (((j).start == (i).end) || ((i).start == (j).end)))

// i contains j
#define contains(i, j) \
  (intvl_eq(intersection(i, j), j))

#define contains_point(i, p) \
  ((p) >= (i).start && (p) < (i).end)

// i and j overlap
#define overlaps(i, j) \
  (!is_null_set(intersection(i, j)))


typedef struct interval {
  uint64_t start;
  uint64_t end;
} interval_t;

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
