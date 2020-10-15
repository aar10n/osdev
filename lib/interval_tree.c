//
// Created by Aaron Gill-Braun on 2020-10-06.
//

#include "interval_tree.h"

#ifndef _assert
#include <panic.h>
#define _assert(expr) kassert((expr))
#endif

#ifndef _malloc
#include <mm/heap.h>
#include <stdio.h>
#define _malloc(size) kmalloc(size)
#define _free(ptr) kfree(ptr)
#endif


#define NULL_SET ((interval_t){ UINT64_MAX, 0 })

#define is_null_set(i) \
  ((i).start == UINT64_MAX && (i).end == 0)

static inline interval_t intersection(interval_t i, interval_t j) {
  if (j.start > i.end || i.start > j.end) {
    return NULL_SET;
  }
  return intvl(max(i.start, j.start), min(i.end, j.end));
}

static inline bool overlaps(interval_t i, interval_t j) {
  return !is_null_set(intersection(i, j));
}

static inline bool equals(interval_t i, interval_t j) {
  return i.start == j.start && i.end == j.end;
}

static inline bool less(interval_t i, interval_t j) {
  return i.start < j.start;
}

static inline bool contiguous(interval_t i, interval_t j) {
  return i.end == j.start;
}

static inline uint64_t midpoint(interval_t i) {
  return (i.start + i.end) / 2;
}

static inline interval_t get_interval(rb_node_t *node) {
  if (node == NULL || node->data == NULL) {
    return NULL_SET;
  }
  return ((intvl_node_t *) node->data)->interval;
}

static inline uint64_t get_max(rb_node_t *node) {
  if (node == NULL || node->data == NULL) {
    return 0;
  }
  return ((intvl_node_t *) node->data)->max;
}

//

void recalculate_max(rb_tree_t *tree, rb_node_t *x) {
  while (x != tree->nil) {
    intvl_node_t *xd = x->data;
    xd->max = max(xd->interval.end, max(get_max(x->left), get_max(x->right)));

    x = x->parent;
  }
}

void post_rotate_callback(rb_tree_t *tree, rb_node_t *x, rb_node_t *y) {
  intvl_node_t *xd = x->data;
  intvl_node_t *yd = y->data;

  yd->max = xd->max;
  recalculate_max(tree, x);
}

void post_insert_callback(rb_tree_t *tree, rb_node_t *z) {
  recalculate_max(tree, z);
}

void post_delete_callback(rb_tree_t *tree, rb_node_t *z, rb_node_t *x) {
  if (z != tree->nil) {
    _free(((intvl_node_t *) z->data)->data);
  }
}

void replace_node_callback(rb_tree_t *tree, rb_node_t *u, rb_node_t *v) {
  intvl_node_t *ud = u->data;
  intvl_node_t *vd = v->data;
  vd->max = ud->max;
}

//

rb_node_t *interval_search(rb_tree_t *tree, interval_t i) {
  rb_node_t *x = tree->root;
  while (x != tree->nil && !overlaps(i, get_interval(x))) {
    if (x->left != tree->nil && get_max(x->left) >= i.start) {
      x = x->left;
    } else {
      x = x->right;
    }
  }
  return x;
}

//

intvl_tree_t *create_intvl_tree() {
  rb_tree_t *rb_tree = create_rb_tree();
  rb_tree_events_t *events = kmalloc(sizeof(rb_tree_events_t));
  events->post_rotate = post_rotate_callback;
  events->post_insert_node = post_insert_callback;
  events->post_delete_node = post_delete_callback;
  events->replace_node = replace_node_callback;
  rb_tree->events = events;

  intvl_tree_t *tree = _malloc(sizeof(intvl_tree_t));
  tree->tree = rb_tree;
  return tree;
}

intvl_node_t *intvl_tree_search(intvl_tree_t *tree, interval_t interval) {
  rb_node_t *node = interval_search(tree->tree, interval);
  return node->data;
}

void intvl_tree_insert(intvl_tree_t *tree, interval_t interval, void *data) {
  intvl_node_t *node_data = _malloc(sizeof(intvl_node_t));
  node_data->interval = interval;
  node_data->data = data;
  rb_tree_insert(tree->tree, interval.start, node_data);
}

void intvl_tree_delete(intvl_tree_t *tree, interval_t interval) {
  rb_tree_delete(tree->tree, interval.start);
}

//

intvl_iter_t *intvl_iter_tree(intvl_tree_t *tree) {
  return rb_tree_iter(tree->tree);
}

intvl_node_t *intvl_iter_next(intvl_iter_t *iter) {
  if (!iter->has_next) {
    return NULL;
  }

  rb_node_t *node = rb_iter_next(iter);
  return node->data;
}
