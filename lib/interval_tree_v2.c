//
// Augmented interval tree (v2)
//

#include "interval_tree_v2.h"
#include <kernel/panic.h>

#define ASSERT(x) kassert(x)

// ============================================================
// augmented data maintenance
// ============================================================

static void intvl_v2_augment(rb_node_v2_t *rb) {
  intvl_node_v2_t *n = intvl_v2_from_rb(rb);
  uint64_t smax = n->end;
  uint64_t smin = n->start;

  if (rb->left) {
    intvl_node_v2_t *l = intvl_v2_from_rb(rb->left);
    if (l->subtree_max > smax) smax = l->subtree_max;
    if (l->subtree_min < smin) smin = l->subtree_min;
  }
  if (rb->right) {
    intvl_node_v2_t *r = intvl_v2_from_rb(rb->right);
    if (r->subtree_max > smax) smax = r->subtree_max;
    if (r->subtree_min < smin) smin = r->subtree_min;
  }

  n->subtree_max = smax;
  n->subtree_min = smin;
}

// ============================================================
// comparison functions for the underlying rb tree
// ============================================================

static int intvl_v2_cmp(const rb_node_v2_t *a, const rb_node_v2_t *b) {
  const intvl_node_v2_t *ia = container_of(a, intvl_node_v2_t, rb);
  const intvl_node_v2_t *ib = container_of(b, intvl_node_v2_t, rb);
  if (ia->start < ib->start) return -1;
  if (ia->start > ib->start) return 1;
  return 0;
}

static int intvl_v2_key_cmp(uint64_t key, const rb_node_v2_t *b) {
  const intvl_node_v2_t *ib = container_of(b, intvl_node_v2_t, rb);
  if (key < ib->start) return -1;
  if (key > ib->start) return 1;
  return 0;
}

// ============================================================
// public: init
// ============================================================

void intvl_tree_v2_init(intvl_tree_v2_t *tree) {
  rb_tree_v2_init(&tree->rb, intvl_v2_cmp, intvl_v2_key_cmp);
  tree->rb.augment = intvl_v2_augment;
}

// ============================================================
// public: find
// ============================================================

intvl_node_v2_t *intvl_tree_v2_find(intvl_tree_v2_t *tree, uint64_t start, uint64_t end) {
  rb_node_v2_t *rb = tree->rb.root;
  while (rb) {
    intvl_node_v2_t *n = intvl_v2_from_rb(rb);
    // check overlap: intervals overlap if n->start < end && start < n->end
    if (n->start < end && start < n->end)
      return n;

    if (rb->left) {
      intvl_node_v2_t *l = intvl_v2_from_rb(rb->left);
      if (l->subtree_max > start) {
        rb = rb->left;
        continue;
      }
    }
    rb = rb->right;
  }
  return NULL;
}

intvl_node_v2_t *intvl_tree_v2_get_point(intvl_tree_v2_t *tree, uint64_t point) {
  return intvl_tree_v2_find(tree, point, point + 1);
}

// ============================================================
// public: insert
// ============================================================

void intvl_tree_v2_insert(intvl_tree_v2_t *tree, intvl_node_v2_t *node) {
  node->subtree_max = node->end;
  node->subtree_min = node->start;
  rb_tree_v2_insert(&tree->rb, &node->rb);
}

// ============================================================
// public: remove
// ============================================================

void intvl_tree_v2_remove(intvl_tree_v2_t *tree, intvl_node_v2_t *node) {
  rb_tree_v2_remove(&tree->rb, &node->rb);
}

// ============================================================
// public: update
// ============================================================

void intvl_tree_v2_update(intvl_tree_v2_t *tree, intvl_node_v2_t *node,
                           int64_t delta_start, int64_t delta_end) {
  if (delta_start != 0) {
    // start changed: BST key changed so we must remove and re-insert
    rb_tree_v2_remove(&tree->rb, &node->rb);
    node->start += delta_start;
    node->end += delta_end;
    node->subtree_max = node->end;
    node->subtree_min = node->start;
    rb_tree_v2_insert(&tree->rb, &node->rb);
  } else {
    node->end += delta_end;
    // walk up to root recalculating augmented data
    rb_node_v2_t *rb = &node->rb;
    while (rb) {
      intvl_v2_augment(rb);
      rb = rb_v2_parent(rb);
    }
  }
}

// ============================================================
// public: find gap
// ============================================================

static always_inline uint64_t align_up(uint64_t val, size_t align) {
  if (align <= 1)
    return val;
  return (val + align - 1) & ~(align - 1);
}

uint64_t intvl_tree_v2_find_gap(intvl_tree_v2_t *tree, uint64_t hint,
                                 uint64_t size, size_t align,
                                 intvl_node_v2_t **prev) {
  if (prev)
    *prev = NULL;

  if (!tree->rb.root) {
    return align_up(hint, align);
  }

  // find the first node with start >= hint
  rb_node_v2_t *rb = tree->rb.root;
  intvl_node_v2_t *ge = NULL;
  while (rb) {
    intvl_node_v2_t *n = intvl_v2_from_rb(rb);
    if (n->start >= hint) {
      ge = n;
      rb = rb->left;
    } else {
      rb = rb->right;
    }
  }

  // find the node immediately before ge (the last node with start < hint)
  intvl_node_v2_t *le = NULL;
  if (ge) {
    rb_node_v2_t *p = rb_tree_v2_prev(&ge->rb);
    le = intvl_v2_from_rb(p);
  } else {
    rb_node_v2_t *last = rb_tree_v2_last(&tree->rb);
    le = intvl_v2_from_rb(last);
  }

  // check gap before ge
  if (ge) {
    uint64_t gap_start;
    if (le) {
      gap_start = le->end > hint ? le->end : hint;
    } else {
      gap_start = hint;
    }

    uint64_t aligned = align_up(gap_start, align);
    if (aligned < ge->start && (ge->start - aligned) >= size) {
      if (prev)
        *prev = le;
      return aligned;
    }
  }

  // walk forward through nodes looking for gaps
  intvl_node_v2_t *cur = ge;
  intvl_node_v2_t *prv = le;

  while (cur) {
    uint64_t gap_start;
    if (prv)
      gap_start = prv->end > hint ? prv->end : hint;
    else
      gap_start = hint;

    uint64_t aligned = align_up(gap_start, align);
    if (aligned < cur->start && (cur->start - aligned) >= size) {
      if (prev)
        *prev = prv;
      return aligned;
    }

    prv = cur;
    rb_node_v2_t *next = rb_tree_v2_next(&cur->rb);
    cur = intvl_v2_from_rb(next);
  }

  // gap after the last node
  uint64_t gap_start = hint;
  if (prv && prv->end > gap_start)
    gap_start = prv->end;

  if (prev)
    *prev = prv;
  return align_up(gap_start, align);
}

// ============================================================
// public: iteration
// ============================================================

intvl_node_v2_t *intvl_tree_v2_first(const intvl_tree_v2_t *tree) {
  return intvl_v2_from_rb(rb_tree_v2_first(&tree->rb));
}

intvl_node_v2_t *intvl_tree_v2_last(const intvl_tree_v2_t *tree) {
  return intvl_v2_from_rb(rb_tree_v2_last(&tree->rb));
}

intvl_node_v2_t *intvl_tree_v2_next(const intvl_node_v2_t *node) {
  return intvl_v2_from_rb(rb_tree_v2_next(&node->rb));
}

intvl_node_v2_t *intvl_tree_v2_prev(const intvl_node_v2_t *node) {
  return intvl_v2_from_rb(rb_tree_v2_prev(&node->rb));
}
