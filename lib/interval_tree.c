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

#define callback(cb, args...) \
  if (tree->events && tree->events->cb) {   \
    tree->events->cb(args);                 \
  }                                         \
  NULL


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

static inline uint64_t get_min(rb_node_t *node) {
  if (node == NULL || node->data == NULL) {
    return UINT64_MAX;
  }
  return ((intvl_node_t *) node->data)->min;
}

//

void recalculate_min_max(rb_tree_t *tree, rb_node_t *x) {
  while (x != tree->nil) {
    intvl_node_t *xd = x->data;
    xd->max = max(xd->interval.end, max(get_max(x->left), get_max(x->right)));
    xd->min = min(xd->interval.start, min(get_min(x->left), get_min(x->right)));
    x = x->parent;
  }
}

void post_rotate_callback(rb_tree_t *tree, rb_node_t *x, rb_node_t *y) {
  intvl_node_t *xd = x->data;
  intvl_node_t *yd = y->data;

  yd->max = xd->max;
  yd->min = xd->min;
  recalculate_min_max(tree, x);
}

void post_insert_callback(rb_tree_t *tree, rb_node_t *z) {
  intvl_node_t *data = z->data;
  data->node = z;
  recalculate_min_max(tree, z);
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
  vd->min = ud->min;
}

void duplicate_node_callback(rb_tree_t *tree, rb_tree_t *new_tree, rb_node_t *u, rb_node_t *v) {
  if (u->data) {
    intvl_node_t *ud = u->data;
    intvl_node_t *vd = _malloc(sizeof(intvl_node_t));
    vd->events = ud->events;
    vd->node = v;
    vd->interval = ud->interval;
    vd->min = ud->min;
    vd->max = ud->max;

    if (ud->data && ud->events && ud->events->copy_data) {
      vd->data = ud->events->copy_data(ud->data);
    }
    v->data = vd;
  }
}

//

intvl_tree_t *create_intvl_tree() {
  rb_tree_t *rb_tree = create_rb_tree();
  rb_tree_events_t *events = kmalloc(sizeof(rb_tree_events_t));
  events->post_rotate = post_rotate_callback;
  events->post_insert_node = post_insert_callback;
  events->post_delete_node = post_delete_callback;
  events->replace_node = replace_node_callback;
  events->duplicate_node = duplicate_node_callback;

  rb_tree->events = events;

  intvl_tree_t *tree = _malloc(sizeof(intvl_tree_t));
  tree->tree = rb_tree;
  tree->events = NULL;
  return tree;
}

intvl_tree_t *copy_intvl_tree(intvl_tree_t *tree) {
  intvl_tree_t *new_tree = _malloc(sizeof(intvl_tree_t));
  new_tree->tree = copy_rb_tree(tree->tree);
  new_tree->events = tree->events;
  return new_tree;
}

//

intvl_node_t *intvl_tree_find(intvl_tree_t *tree, interval_t interval) {
  rb_tree_t *rb = tree->tree;
  interval_t i = interval;

  rb_node_t *node = rb->root;
  while (node != rb->nil && !overlaps(i, get_interval(node))) {
    if (node->left != rb->nil && get_max(node->left) > i.start) {
      node = node->left;
    } else {
      node = node->right;
    }
  }

  return node->data;
}

intvl_node_t *intvl_tree_find_closest(intvl_tree_t *tree, interval_t interval) {
  rb_tree_t *rb = tree->tree;
  interval_t i = interval;

  rb_node_t *closest = NULL;
  rb_node_t *node = rb->root;
  while (node != rb->nil) {
    if (overlaps(i, get_interval(node))) {
      return node->data;
    }

    closest = node;
    if (overlaps(i, get_interval(node->left))) {
      return node->left->data;
    } else if (overlaps(i, get_interval(node->right))) {
      return node->right->data;
    } else {
      uint64_t diff = i.start < get_interval(node).start ?
                      udiff(i.end, get_interval(node).start) :
                      udiff(i.start, get_interval(node).end);

      uint64_t ldiff = min(
        udiff(get_min(node->left), i.start),
        udiff(get_max(node->left), i.end)
      );
      uint64_t rdiff = min(
        udiff(get_min(node->right), i.start),
        udiff(get_max(node->right), i.end)
      );
      if (diff <= ldiff && diff <= rdiff) {
        // current node is closest
        break;
      } else if (ldiff <= rdiff) {
        node = node->left;
      } else {
        node = node->right;
      }
    }
  }

  return closest->data;
}

void intvl_tree_insert(intvl_tree_t *tree, interval_t interval, void *data) {
  intvl_node_t *node_data = _malloc(sizeof(intvl_node_t));
  node_data->events = tree->events;
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
