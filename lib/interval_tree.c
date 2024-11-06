//
// Created by Aaron Gill-Braun on 2020-10-06.
//

#include "interval_tree.h"

#include <kernel/mm/heap.h>
#include <kernel/string.h>
#include <kernel/panic.h>
#include <kernel/printf.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("intvl_tree: " fmt, ##__VA_ARGS__)


static inline interval_t get_interval(rb_tree_t *tree, rb_node_t *node) {
  if (node == NULL || node == tree->nil || node->data == NULL) {
    return NULL_SET;
  }
  return ((intvl_node_t *) node->data)->interval;
}

static inline uint64_t get_max(rb_tree_t *tree, rb_node_t *node) {
  if (node == NULL || node == tree->nil || node->data == NULL) {
    return 0;
  }
  return ((intvl_node_t *) node->data)->max;
}

static inline uint64_t get_min(rb_tree_t *tree, rb_node_t *node) {
  if (node == NULL || node == tree->nil || node->data == NULL) {
    return UINT64_MAX;
  }
  return ((intvl_node_t *) node->data)->min;
}

static bool check_gap(uint64_t gap_start, uint64_t gap_end, uint64_t size, size_t align) {
  uint64_t aligned_start = align(gap_start, align);
  if (aligned_start < gap_end && (gap_end - aligned_start) >= size) {
    return true;
  }
  return false;
}

static interval_t make_aligned_interval(uint64_t start, uint64_t size, size_t align) {
  uint64_t aligned_start = align(start, align);
  return intvl(aligned_start, aligned_start + size);
}

//

void recalculate_min_max(rb_tree_t *tree, rb_node_t *x) {
  while (x != tree->nil) {
    intvl_node_t *xd = x->data;
    xd->max = max(xd->interval.end, max(get_max(tree, x->left), get_max(tree, x->right)));
    xd->min = min(xd->interval.start, min(get_min(tree, x->left), get_min(tree, x->right)));
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
  // if (z != tree->nil) {
  //   _free(((intvl_node_t *) z->data)->data);
  // }
}

void replace_node_callback(rb_tree_t *tree, rb_node_t *u, rb_node_t *v) {
  if (v->data) {
    intvl_node_t *ud = u->data;
    intvl_node_t *vd = v->data;
    vd->max = ud->max;
    vd->min = ud->min;
  }
}

void duplicate_node_callback(rb_tree_t *tree, rb_tree_t *new_tree, rb_node_t *u, rb_node_t *v) {
  if (u->data) {
    intvl_node_t *ud = u->data;
    intvl_node_t *vd = kmallocz(sizeof(intvl_node_t));
    vd->node = v;
    vd->interval = ud->interval;
    vd->min = ud->min;
    vd->max = ud->max;
    v->data = vd;
  }
}

//

intvl_tree_t *create_intvl_tree() {
  rb_tree_t *tree = create_rb_tree();
  rb_tree_events_t *events = kmallocz(sizeof(rb_tree_events_t));
  events->post_rotate = post_rotate_callback;
  events->post_insert_node = post_insert_callback;
  events->post_delete_node = post_delete_callback;
  events->replace_node = replace_node_callback;
  events->duplicate_node = duplicate_node_callback;

  tree->events = events;
  return tree;
}

//

intvl_node_t *intvl_tree_find(intvl_tree_t *tree, interval_t interval) {
  rb_tree_t *rb = tree;
  interval_t i = interval;

  rb_node_t *node = rb->root;
  while (node != rb->nil && !overlaps(i, get_interval(rb, node))) {
    if (node->left != rb->nil && get_max(rb, node->left) > i.start) {
      node = node->left;
    } else {
      node = node->right;
    }
  }

  return node->data;
}

void *intvl_tree_get_point(intvl_tree_t *tree, uint64_t point) {
  rb_tree_t *rb = tree;
  interval_t i = intvl(point, point+1);

  rb_node_t *node = rb->root;
  while (node != rb->nil && !overlaps(i, get_interval(rb, node))) {
    if (node->left != rb->nil && get_max(rb, node->left) > point) {
      node = node->left;
    } else {
      node = node->right;
    }
  }

  if (node != rb->nil) {
    return ((intvl_node_t *) node->data)->data;
  }
  return NULL;
}

/// Returns an interval representing the next non-occupied range in the tree with
/// the same size as the given interval and a start point greater than or equal to
/// the given interval's start point.
interval_t intvl_tree_find_free_gap(intvl_tree_t *tree, interval_t intvl, size_t align, intvl_node_t **prev_node) {
  rb_tree_t *rb = tree;
  uint64_t size = intvl.end - intvl.start;
  if (align == 0)
    align = 1;

  if (prev_node) {
    *prev_node = NULL;
  }

  if (rb->root == rb->nil) {
    return intvl;
  }

  // Find the first node whose start point is >= intvl.start
  rb_node_t *current = rb->root;
  rb_node_t *closest_greater = rb->nil;

  while (current != rb->nil) {
    intvl_node_t *node_data = (intvl_node_t *)current->data;
    if (node_data->interval.start >= intvl.start) {
      closest_greater = current;
      current = current->left;
    } else {
      current = current->right;
    }
  }

  // Find the node with the greatest start point < intvl.start
  rb_node_t *closest_lesser = rb->nil;
  if (closest_greater != rb->nil) {
    closest_lesser = closest_greater->prev;
  } else {
    closest_lesser = rb->max;
  }

  // Check gap before the closest_greater interval
  if (closest_greater != rb->nil) {
    intvl_node_t *greater_data = (intvl_node_t *)closest_greater->data;
    uint64_t gap_start;

    if (closest_lesser != rb->nil) {
      intvl_node_t *lesser_data = (intvl_node_t *)closest_lesser->data;
      gap_start = max(intvl.start, lesser_data->interval.end);
      if (check_gap(gap_start, greater_data->interval.start, size, align)) {
        if (prev_node) {
          *prev_node = lesser_data;
        }
        return make_aligned_interval(gap_start, size, align);
      }
    } else {
      gap_start = intvl.start;
      if (check_gap(gap_start, greater_data->interval.start, size, align)) {
        if (prev_node) {
          *prev_node = NULL;
        }
        return make_aligned_interval(gap_start, size, align);
      }
    }
  }

  // Walk forward through the nodes looking for gaps
  current = closest_greater;
  rb_node_t *prev = closest_lesser;
  uint64_t current_start = intvl.start;

  while (current != rb->nil) {
    intvl_node_t *curr_data = (intvl_node_t *)current->data;
    uint64_t gap_start;

    if (prev != rb->nil) {
      intvl_node_t *prev_data = (intvl_node_t *)prev->data;
      gap_start = max(current_start, prev_data->interval.end);

      if (check_gap(gap_start, curr_data->interval.start, size, align)) {
        if (prev_node) {
          *prev_node = prev_data;
        }
        return make_aligned_interval(gap_start, size, align);
      }
    } else {
      gap_start = current_start;

      if (check_gap(gap_start, curr_data->interval.start, size, align)) {
        if (prev_node) {
          *prev_node = NULL;
        }
        return make_aligned_interval(gap_start, size, align);
      }
    }

    current_start = max(current_start, curr_data->interval.end);
    prev = current;
    current = current->next;
  }

  // Check if there's space after the last interval
  if (prev != rb->nil) {
    intvl_node_t *prev_data = (intvl_node_t *)prev->data;
    uint64_t gap_start = max(current_start, prev_data->interval.end);
    if (prev_node) {
      *prev_node = prev_data;
    }
    return make_aligned_interval(gap_start, size, align);
  }

  // No previous intervals
  if (prev_node) {
    *prev_node = NULL;
  }
  return make_aligned_interval(current_start, size, align);
}

void intvl_tree_insert(intvl_tree_t *tree, interval_t interval, void *data) {
  intvl_node_t *node_data = kmalloc(sizeof(intvl_node_t));
  node_data->interval = interval;
  node_data->data = data;
  rb_tree_insert(tree, interval.start, node_data);
}

void intvl_tree_delete(intvl_tree_t *tree, interval_t interval) {
  rb_tree_delete(tree, interval.start);
}

void intvl_tree_update_interval(intvl_tree_t *tree, intvl_node_t *node, off_t ds, off_t de) {
  node->interval.start += ds;
  node->interval.end += de;
  node->min = min(node->min, node->interval.start);
  node->max = max(node->max, node->interval.end);
  recalculate_min_max(tree, node->node);
}
