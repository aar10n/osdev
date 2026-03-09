//
// Augmented interval tree (v2)
//
// Built on rb_tree_v2. Embed intvl_node_v2_t in your struct.
// The tree maintains subtree min/max automatically during
// insert, remove, and update operations.
//

#ifndef LIB_INTERVAL_TREE_V2_H
#define LIB_INTERVAL_TREE_V2_H

#include <rb_tree_v2.h>

typedef struct intvl_node_v2 {
  rb_node_v2_t rb;
  uint64_t start;
  uint64_t end;
  uint64_t subtree_max;
  uint64_t subtree_min;
} intvl_node_v2_t;

typedef struct intvl_tree_v2 {
  rb_tree_v2_t rb;
} intvl_tree_v2_t;

static always_inline intvl_node_v2_t *intvl_v2_from_rb(rb_node_v2_t *n) {
  return n ? container_of(n, intvl_node_v2_t, rb) : NULL;
}

void intvl_tree_v2_init(intvl_tree_v2_t *tree);

static always_inline bool intvl_tree_v2_empty(const intvl_tree_v2_t *tree) {
  return rb_tree_v2_empty(&tree->rb);
}

static always_inline size_t intvl_tree_v2_count(const intvl_tree_v2_t *tree) {
  return tree->rb.nodes;
}

// find any node whose interval overlaps [start, end)
intvl_node_v2_t *intvl_tree_v2_find(intvl_tree_v2_t *tree, uint64_t start, uint64_t end);

// find the node whose interval contains point
intvl_node_v2_t *intvl_tree_v2_get_point(intvl_tree_v2_t *tree, uint64_t point);

// insert a node (caller must set node->start and node->end before calling)
void intvl_tree_v2_insert(intvl_tree_v2_t *tree, intvl_node_v2_t *node);

// remove a node
void intvl_tree_v2_remove(intvl_tree_v2_t *tree, intvl_node_v2_t *node);

// adjust a node's interval in-place and fix augmented data.
// if delta_start != 0, the node is removed and re-inserted to
// maintain BST ordering.
void intvl_tree_v2_update(intvl_tree_v2_t *tree, intvl_node_v2_t *node,
                           int64_t delta_start, int64_t delta_end);

// find a free gap of at least `size` bytes with the given alignment,
// starting the search at `hint`. returns the start of the gap, or
// UINT64_MAX if no gap is found.
// if `prev` is non-NULL, *prev is set to the node immediately before the gap.
uint64_t intvl_tree_v2_find_gap(intvl_tree_v2_t *tree, uint64_t hint,
                                 uint64_t size, size_t align,
                                 intvl_node_v2_t **prev);

// iteration
intvl_node_v2_t *intvl_tree_v2_first(const intvl_tree_v2_t *tree);
intvl_node_v2_t *intvl_tree_v2_last(const intvl_tree_v2_t *tree);
intvl_node_v2_t *intvl_tree_v2_next(const intvl_node_v2_t *node);
intvl_node_v2_t *intvl_tree_v2_prev(const intvl_node_v2_t *node);

#define intvl_tree_v2_foreach(pos, tree) \
  for ((pos) = intvl_tree_v2_first(tree); (pos); (pos) = intvl_tree_v2_next(pos))

#define intvl_tree_v2_foreach_safe(pos, tmp, tree) \
  for ((pos) = intvl_tree_v2_first(tree), (tmp) = (pos) ? intvl_tree_v2_next(pos) : NULL; \
       (pos); \
       (pos) = (tmp), (tmp) = (pos) ? intvl_tree_v2_next(pos) : NULL)

#endif
