//
// Intrusive red-black tree (v2)
//

#include "rb_tree_v2.h"
#include <kernel/panic.h>

#define ASSERT(x) kassert(x)

// ============================================================
// internal helpers
// ============================================================

static always_inline void set_child(rb_node_v2_t *parent, rb_node_v2_t *child, bool left) {
  if (left)
    parent->left = child;
  else
    parent->right = child;
}

static always_inline void augment(rb_tree_v2_t *tree, rb_node_v2_t *node) {
  if (tree->augment)
    tree->augment(node);
}

static always_inline void augment_walk(rb_tree_v2_t *tree, rb_node_v2_t *node) {
  if (!tree->augment)
    return;
  while (node) {
    tree->augment(node);
    node = rb_v2_parent(node);
  }
}

// ============================================================
// rotations
// ============================================================

static void rotate_left(rb_tree_v2_t *tree, rb_node_v2_t *x) {
  rb_node_v2_t *y = x->right;
  rb_node_v2_t *p = rb_v2_parent(x);

  x->right = y->left;
  if (y->left)
    rb_v2_set_parent(y->left, x);

  rb_v2_set_parent(y, p);
  if (!p)
    tree->root = y;
  else if (x == p->left)
    p->left = y;
  else
    p->right = y;

  y->left = x;
  rb_v2_set_parent(x, y);

  augment(tree, x);
  augment(tree, y);
}

static void rotate_right(rb_tree_v2_t *tree, rb_node_v2_t *x) {
  rb_node_v2_t *y = x->left;
  rb_node_v2_t *p = rb_v2_parent(x);

  x->left = y->right;
  if (y->right)
    rb_v2_set_parent(y->right, x);

  rb_v2_set_parent(y, p);
  if (!p)
    tree->root = y;
  else if (x == p->left)
    p->left = y;
  else
    p->right = y;

  y->right = x;
  rb_v2_set_parent(x, y);

  augment(tree, x);
  augment(tree, y);
}

// ============================================================
// insert fixup
// ============================================================

static void insert_fixup(rb_tree_v2_t *tree, rb_node_v2_t *z) {
  while (rb_v2_is_red(rb_v2_parent(z))) {
    rb_node_v2_t *p = rb_v2_parent(z);
    rb_node_v2_t *gp = rb_v2_parent(p);

    if (p == gp->left) {
      rb_node_v2_t *uncle = gp->right;
      if (rb_v2_is_red(uncle)) {
        rb_v2_set_color(p, RB_V2_BLACK);
        rb_v2_set_color(uncle, RB_V2_BLACK);
        rb_v2_set_color(gp, RB_V2_RED);
        z = gp;
      } else {
        if (z == p->right) {
          z = p;
          rotate_left(tree, z);
          p = rb_v2_parent(z);
          gp = rb_v2_parent(p);
        }
        rb_v2_set_color(p, RB_V2_BLACK);
        rb_v2_set_color(gp, RB_V2_RED);
        rotate_right(tree, gp);
      }
    } else {
      rb_node_v2_t *uncle = gp->left;
      if (rb_v2_is_red(uncle)) {
        rb_v2_set_color(p, RB_V2_BLACK);
        rb_v2_set_color(uncle, RB_V2_BLACK);
        rb_v2_set_color(gp, RB_V2_RED);
        z = gp;
      } else {
        if (z == p->left) {
          z = p;
          rotate_right(tree, z);
          p = rb_v2_parent(z);
          gp = rb_v2_parent(p);
        }
        rb_v2_set_color(p, RB_V2_BLACK);
        rb_v2_set_color(gp, RB_V2_RED);
        rotate_left(tree, gp);
      }
    }
  }
  rb_v2_set_color(tree->root, RB_V2_BLACK);
}

// ============================================================
// replace: transplant v into u's position
// ============================================================

static void replace_node(rb_tree_v2_t *tree, rb_node_v2_t *u, rb_node_v2_t *v) {
  rb_node_v2_t *p = rb_v2_parent(u);
  if (!p)
    tree->root = v;
  else if (u == p->left)
    p->left = v;
  else
    p->right = v;
  if (v)
    rb_v2_set_parent(v, p);
}

// ============================================================
// delete fixup
// ============================================================

static void delete_fixup(rb_tree_v2_t *tree, rb_node_v2_t *x, rb_node_v2_t *x_parent) {
  // x may be NULL (the nil leaf). x_parent tracks the parent in that case.
  while (x != tree->root && rb_v2_is_black(x)) {
    if (x == x_parent->left) {
      rb_node_v2_t *w = x_parent->right;
      if (rb_v2_is_red(w)) {
        rb_v2_set_color(w, RB_V2_BLACK);
        rb_v2_set_color(x_parent, RB_V2_RED);
        rotate_left(tree, x_parent);
        w = x_parent->right;
      }
      if (rb_v2_is_black(w->left) && rb_v2_is_black(w->right)) {
        rb_v2_set_color(w, RB_V2_RED);
        x = x_parent;
        x_parent = rb_v2_parent(x);
      } else {
        if (rb_v2_is_black(w->right)) {
          rb_v2_set_color(w->left, RB_V2_BLACK);
          rb_v2_set_color(w, RB_V2_RED);
          rotate_right(tree, w);
          w = x_parent->right;
        }
        rb_v2_set_color(w, rb_v2_color(x_parent));
        rb_v2_set_color(x_parent, RB_V2_BLACK);
        rb_v2_set_color(w->right, RB_V2_BLACK);
        rotate_left(tree, x_parent);
        x = tree->root;
        break;
      }
    } else {
      rb_node_v2_t *w = x_parent->left;
      if (rb_v2_is_red(w)) {
        rb_v2_set_color(w, RB_V2_BLACK);
        rb_v2_set_color(x_parent, RB_V2_RED);
        rotate_right(tree, x_parent);
        w = x_parent->left;
      }
      if (rb_v2_is_black(w->right) && rb_v2_is_black(w->left)) {
        rb_v2_set_color(w, RB_V2_RED);
        x = x_parent;
        x_parent = rb_v2_parent(x);
      } else {
        if (rb_v2_is_black(w->left)) {
          rb_v2_set_color(w->right, RB_V2_BLACK);
          rb_v2_set_color(w, RB_V2_RED);
          rotate_left(tree, w);
          w = x_parent->left;
        }
        rb_v2_set_color(w, rb_v2_color(x_parent));
        rb_v2_set_color(x_parent, RB_V2_BLACK);
        rb_v2_set_color(w->left, RB_V2_BLACK);
        rotate_right(tree, x_parent);
        x = tree->root;
        break;
      }
    }
  }
  if (x)
    rb_v2_set_color(x, RB_V2_BLACK);
}

// ============================================================
// subtree min/max
// ============================================================

static rb_node_v2_t *subtree_min(rb_node_v2_t *x) {
  while (x->left)
    x = x->left;
  return x;
}

static rb_node_v2_t *subtree_max(rb_node_v2_t *x) {
  while (x->right)
    x = x->right;
  return x;
}

// ============================================================
// public: insert
// ============================================================

void rb_tree_v2_insert(rb_tree_v2_t *tree, rb_node_v2_t *node) {
  node->left = NULL;
  node->right = NULL;

  rb_node_v2_t *parent = NULL;
  rb_node_v2_t **link = &tree->root;

  while (*link) {
    parent = *link;
    int cmp = tree->cmp(node, parent);
    if (cmp < 0)
      link = &parent->left;
    else
      link = &parent->right;
  }

  *link = node;
  rb_v2_set_parent_color(node, parent, RB_V2_RED);
  tree->nodes++;

  insert_fixup(tree, node);
  augment_walk(tree, node);
}

// ============================================================
// public: remove
// ============================================================

void rb_tree_v2_remove(rb_tree_v2_t *tree, rb_node_v2_t *z) {
  rb_node_v2_t *x;        // the node that replaces the physically removed node
  rb_node_v2_t *x_parent; // parent of x (needed when x is NULL)
  int orig_color;

  if (!z->left) {
    x = z->right;
    x_parent = rb_v2_parent(z);
    orig_color = rb_v2_color(z);
    replace_node(tree, z, z->right);
  } else if (!z->right) {
    x = z->left;
    x_parent = rb_v2_parent(z);
    orig_color = rb_v2_color(z);
    replace_node(tree, z, z->left);
  } else {
    // two children: replace z with its in-order successor y
    rb_node_v2_t *y = subtree_min(z->right);
    orig_color = rb_v2_color(y);
    x = y->right;

    if (rb_v2_parent(y) == z) {
      x_parent = y;
    } else {
      x_parent = rb_v2_parent(y);
      replace_node(tree, y, y->right);
      y->right = z->right;
      rb_v2_set_parent(y->right, y);
    }

    replace_node(tree, z, y);
    y->left = z->left;
    rb_v2_set_parent(y->left, y);
    rb_v2_set_color(y, rb_v2_color(z));
  }

  tree->nodes--;

  if (orig_color == RB_V2_BLACK)
    delete_fixup(tree, x, x_parent);

  augment_walk(tree, x_parent);
}

// ============================================================
// public: find
// ============================================================

rb_node_v2_t *rb_tree_v2_find(rb_tree_v2_t *tree, uint64_t key) {
  rb_node_v2_t *node = tree->root;
  while (node) {
    int cmp = tree->key_cmp(key, node);
    if (cmp == 0)
      return node;
    else if (cmp < 0)
      node = node->left;
    else
      node = node->right;
  }
  return NULL;
}

rb_node_v2_t *rb_tree_v2_find_ge(rb_tree_v2_t *tree, uint64_t key) {
  rb_node_v2_t *node = tree->root;
  rb_node_v2_t *best = NULL;
  while (node) {
    int cmp = tree->key_cmp(key, node);
    if (cmp == 0)
      return node;
    else if (cmp < 0) {
      best = node;
      node = node->left;
    } else {
      node = node->right;
    }
  }
  return best;
}

rb_node_v2_t *rb_tree_v2_find_le(rb_tree_v2_t *tree, uint64_t key) {
  rb_node_v2_t *node = tree->root;
  rb_node_v2_t *best = NULL;
  while (node) {
    int cmp = tree->key_cmp(key, node);
    if (cmp == 0)
      return node;
    else if (cmp < 0) {
      node = node->left;
    } else {
      best = node;
      node = node->right;
    }
  }
  return best;
}

// ============================================================
// public: iteration
// ============================================================

rb_node_v2_t *rb_tree_v2_first(const rb_tree_v2_t *tree) {
  if (!tree->root)
    return NULL;
  return subtree_min(tree->root);
}

rb_node_v2_t *rb_tree_v2_last(const rb_tree_v2_t *tree) {
  if (!tree->root)
    return NULL;
  return subtree_max(tree->root);
}

rb_node_v2_t *rb_tree_v2_next(const rb_node_v2_t *node) {
  if (node->right)
    return subtree_min(node->right);

  const rb_node_v2_t *n = node;
  rb_node_v2_t *p = rb_v2_parent(n);
  while (p && n == p->right) {
    n = p;
    p = rb_v2_parent(p);
  }
  return p;
}

rb_node_v2_t *rb_tree_v2_prev(const rb_node_v2_t *node) {
  if (node->left)
    return subtree_max(node->left);

  const rb_node_v2_t *n = node;
  rb_node_v2_t *p = rb_v2_parent(n);
  while (p && n == p->left) {
    n = p;
    p = rb_v2_parent(p);
  }
  return p;
}
