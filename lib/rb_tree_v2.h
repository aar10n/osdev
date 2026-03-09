//
// Intrusive red-black tree (v2)
//
// Embed rb_node_v2_t in your struct. Use container_of() to recover the
// containing struct from a node pointer. Color is stored in the low
// bit of the parent pointer.
//

#ifndef LIB_RB_TREE_V2_H
#define LIB_RB_TREE_V2_H

#include <kernel/base.h>

typedef struct rb_node_v2 {
  uintptr_t __parent_color;
  struct rb_node_v2 *left;
  struct rb_node_v2 *right;
} rb_node_v2_t;

// node-vs-node comparison for insert: return <0, 0, >0
typedef int (*rb_v2_cmp_fn)(const rb_node_v2_t *a, const rb_node_v2_t *b);
// key-vs-node comparison for lookup: return <0, 0, >0
typedef int (*rb_v2_key_cmp_fn)(uint64_t key, const rb_node_v2_t *node);

// optional callback invoked after the tree structure changes at a node
// (rotations, insert fixup, delete fixup). used by the interval tree to
// recalculate augmented data. called with the node whose subtree changed.
typedef void (*rb_v2_augment_fn)(rb_node_v2_t *node);

typedef struct rb_tree_v2 {
  rb_node_v2_t *root;
  size_t nodes;
  rb_v2_cmp_fn cmp;
  rb_v2_key_cmp_fn key_cmp;
  rb_v2_augment_fn augment;
} rb_tree_v2_t;

// -- parent/color encoding --

#define RB_V2_RED   0
#define RB_V2_BLACK 1

static always_inline rb_node_v2_t *rb_v2_parent(const rb_node_v2_t *n) {
  return (rb_node_v2_t *)(n->__parent_color & ~(uintptr_t)1);
}

static always_inline int rb_v2_color(const rb_node_v2_t *n) {
  return (int)(n->__parent_color & 1);
}

static always_inline bool rb_v2_is_red(const rb_node_v2_t *n) {
  return n && (n->__parent_color & 1) == 0;
}

static always_inline bool rb_v2_is_black(const rb_node_v2_t *n) {
  return !n || (n->__parent_color & 1) != 0;
}

static always_inline void rb_v2_set_parent(rb_node_v2_t *n, rb_node_v2_t *p) {
  n->__parent_color = (uintptr_t)p | (n->__parent_color & 1);
}

static always_inline void rb_v2_set_color(rb_node_v2_t *n, int color) {
  n->__parent_color = (n->__parent_color & ~(uintptr_t)1) | (unsigned)color;
}

static always_inline void rb_v2_set_parent_color(rb_node_v2_t *n, rb_node_v2_t *p, int color) {
  n->__parent_color = (uintptr_t)p | (unsigned)color;
}

// -- initialization --

#define RB_TREE_V2_INIT(cmp_fn, key_cmp_fn) \
  { .root = NULL, .nodes = 0, .cmp = (cmp_fn), .key_cmp = (key_cmp_fn), .augment = NULL }

static always_inline void rb_tree_v2_init(rb_tree_v2_t *tree, rb_v2_cmp_fn cmp, rb_v2_key_cmp_fn key_cmp) {
  tree->root = NULL;
  tree->nodes = 0;
  tree->cmp = cmp;
  tree->key_cmp = key_cmp;
  tree->augment = NULL;
}

static always_inline bool rb_tree_v2_empty(const rb_tree_v2_t *tree) {
  return tree->root == NULL;
}

// -- core operations --

void rb_tree_v2_insert(rb_tree_v2_t *tree, rb_node_v2_t *node);
void rb_tree_v2_remove(rb_tree_v2_t *tree, rb_node_v2_t *node);
rb_node_v2_t *rb_tree_v2_find(rb_tree_v2_t *tree, uint64_t key);
rb_node_v2_t *rb_tree_v2_find_ge(rb_tree_v2_t *tree, uint64_t key);
rb_node_v2_t *rb_tree_v2_find_le(rb_tree_v2_t *tree, uint64_t key);

// -- iteration --

rb_node_v2_t *rb_tree_v2_first(const rb_tree_v2_t *tree);
rb_node_v2_t *rb_tree_v2_last(const rb_tree_v2_t *tree);
rb_node_v2_t *rb_tree_v2_next(const rb_node_v2_t *node);
rb_node_v2_t *rb_tree_v2_prev(const rb_node_v2_t *node);

#define rb_tree_v2_foreach(pos, tree) \
  for ((pos) = rb_tree_v2_first(tree); (pos); (pos) = rb_tree_v2_next(pos))

#define rb_tree_v2_foreach_safe(pos, tmp, tree) \
  for ((pos) = rb_tree_v2_first(tree), (tmp) = (pos) ? rb_tree_v2_next(pos) : NULL; \
       (pos); \
       (pos) = (tmp), (tmp) = (pos) ? rb_tree_v2_next(pos) : NULL)

#endif
