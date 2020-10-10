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

static inline bool contains(interval_t i, interval_t j) {
  return i.start <= j.start && i.end > j.end;
}

static inline bool equals(interval_t i, interval_t j) {
  return i.start == j.start && i.end == j.end;
}

static inline uint64_t midpoint(interval_t i) {
  return (i.start + i.end) / 2;
}

static inline interval_t make_union(interval_t i, interval_t j) {
  interval_t k;
  k.start = min(i.start, j.start);
  k.end = max(i.end, j.end);
  return k;
}

static inline uint64_t get_node_max(intvl_node_t *node) {
  if (node->right) {
    return get_node_max(node->right);
  }
  return node->interval.end;
}

static inline uint64_t get_node_min(intvl_node_t *node) {
  if (node->left) {
    return get_node_max(node->right);
  }
  return node->interval.start;
}

static inline bool join_nodes(intvl_node_t *parent, intvl_node_t *a, intvl_node_t *b) {
  parent->interval = make_union(a->interval, b->interval);
  parent->is_child = false;
  parent->data = NULL;

  a->parent = parent;
  b->parent = parent;

  uint64_t mid = midpoint(parent->interval);
  if (a->interval.start < mid) {
    if (b->interval.start < mid || a->interval.end >= b->interval.start) {
      // interval overlaps existing one
      return false;
    }
    parent->left = a;
    parent->right = b;
  } else {
    if (a->interval.start < mid || b->interval.end >= a->interval.start) {
      // interval overlaps existing one
      return false;
    }
    parent->left = b;
    parent->right = a;
  }
  return true;
}

//

void free_node(intvl_node_t *node) {
  if (node == NULL) return;
  if (!node->is_child && node->left != NULL) {
    free_node(node->left);
  } else if (!node->is_child && node->right != NULL) {
    free_node(node->right);
  }

  if (node->data) {
    _free(node->data);
  }
  _free(node);
}

//

intvl_node_t *get_node_from_point(intvl_node_t *tree, uint64_t point) {
  intvl_node_t *node = tree;
  interval_t interval = (interval_t){ .start = point, .end = point };
  while (node && contains(node->interval, interval) && !node->is_child) {
    uint64_t mid = midpoint(node->interval);
    if (interval.start < mid) {
      node = node->left;
    } else {
      node = node->right;
    }
  }
  return node;
}

intvl_node_t *get_node_from_interval(intvl_node_t *tree, interval_t interval) {
  intvl_node_t *node = tree;
  while (node && !equals(node->interval, interval)) {
    uint64_t mid = midpoint(node->interval);
    if (interval.start < mid) {
      node = node->left;
    } else {
      node = node->right;
    }
  }
  return node;
}

//

intvl_node_t *create_interval_tree(uint64_t min, uint64_t max) {
  intvl_node_t *node = _malloc(sizeof(intvl_node_t));
  node->interval = (interval_t){ min, max };
  node->is_child = true;
  node->data = NULL;
  node->parent = NULL;
  node->left = NULL;
  node->right = NULL;
  return node;
}

bool tree_add_interval(intvl_node_t *tree, interval_t interval, void *data) {
  _assert(tree != NULL);
  intvl_node_t *node = tree;

  intvl_node_t *new_node = _malloc(sizeof(intvl_node_t));
  new_node->interval = interval;
  new_node->data = data;
  new_node->is_child = true;
  new_node->left = NULL;
  new_node->right = NULL;

  // find the lowest node that fully contains the interval
  intvl_node_t *parent = NULL;
  while (node && contains(node->interval, interval)) {
    uint64_t mid = midpoint(node->interval);
    parent = node;
    if (interval.start < mid) {
      node = node->left;
    } else {
      node = node->right;
    }
  }

  if (node != NULL) {
    // create inner node
    intvl_node_t *inner = _malloc(sizeof(intvl_node_t));
    bool success = join_nodes(inner, new_node, node);
    if (success == false) {
      return false;
    }
    new_node = inner;
  }

  uint64_t mid = midpoint(parent->interval);
  if (new_node->interval.start < mid) {
    parent->left = new_node;
  } else {
    parent->right = new_node;
  }
  return true;
}

bool tree_remove_interval(intvl_node_t *tree, interval_t interval) {
  _assert(tree != NULL);

  intvl_node_t *node = get_node_from_interval(tree, interval);
  if (node == NULL) {
    kprintf("interval (%lld, %lld) not found\n", interval.start, interval.end);
    return false;
  }

  if (!node->parent) {
    return false;
  }

  uint64_t mid = midpoint(node->parent->interval);
  if (node->interval.start < mid) {
    node->parent->left = NULL;
  } else {
    node->parent->right = NULL;
  }
  free_node(node);
  return true;
}

interval_t tree_find_free_interval(intvl_node_t *tree, size_t min_size) {
  _assert(tree != NULL);

  intvl_node_t *node = tree;
  if (!node->left->is_child) {
    interval_t result = tree_find_free_interval(node->left, min_size);
    if (!IS_ERROR_INTERVAL(result)) {
      return result;
    }
  }
  if (!node->right->is_child) {
    interval_t result = tree_find_free_interval(node->right, min_size);
    if (!IS_ERROR_INTERVAL(result)) {
      return result;
    }
  }

  uint64_t start = get_node_max(node->left);
  uint64_t end = get_node_min(node->right) - 1;

  if (start != end && end - start >= min_size) {
    return (interval_t){ start, end };
  }
  return ERROR_INTERVAL;
}

intvl_node_t *tree_query_point(intvl_node_t *tree, uint64_t point) {
  _assert(tree != NULL);
  return get_node_from_point(tree, point);
}

intvl_node_t *tree_query_interval(intvl_node_t *tree, uint64_t start, uint64_t end) {
  _assert(tree != NULL);
  interval_t s = {start, end };
  return get_node_from_interval(tree, s);
}
