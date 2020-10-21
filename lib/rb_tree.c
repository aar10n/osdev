//
// Created by Aaron Gill-Braun on 2020-10-12.
//

#include "rb_tree.h"

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

static inline rb_node_t *minimum(rb_tree_t *tree, rb_node_t *x) {
  while (x->left != tree->nil) {
    x = x->left;
  }
  return x;
}

static inline rb_node_t *get_side(rb_node_t *node, bool left) {
  if (left) {
    return node->left;
  }
  return node->right;
}

// Operations

void rotate_left(rb_tree_t *tree, rb_node_t *x) {
  callback(pre_rotate, tree, x, x->right);

  rb_node_t *y = x->right;
  x->right = y->left;
  if (y->left != tree->nil) {
    y->left->parent = x;
  }

  y->parent = x->parent;
  if (x->parent == tree->nil) {
    tree->root = y;
  } else if (x == x->parent->left) {
    x->parent->left = y;
  } else {
    x->parent->right = y;
  }
  y->left = x;
  x->parent = y;

  callback(post_rotate, tree, x, y);
}

void rotate_right(rb_tree_t *tree, rb_node_t *x) {
  callback(pre_rotate, tree, x, x->left);

  rb_node_t *y = x->left;
  x->left = y->right;
  if (y->right != tree->nil) {
    y->right->parent = x;
  }

  y->parent = x->parent;
  if (x->parent == tree->nil) {
    tree->root = y;
  } else if (x == x->parent->left) {
    x->parent->left = y;
  } else {
    x->parent->right = y;
  }
  y->right = x;
  x->parent = y;

  callback(post_rotate, tree, x, y);
}

void replace_node(rb_tree_t *tree, rb_node_t *u, rb_node_t *v) {
  callback(replace_node, tree, u, v);

  if (u->parent == tree->nil) {
    tree->root = v;
  } else if (u == u->parent->left) {
    u->parent->left = v;
  } else {
    u->parent->right = v;
  }
  v->parent = u->parent;
}

//
// Insertion
//

void insert_fixup(rb_tree_t *tree, rb_node_t *z) {
  rb_node_t *y;
  while (z->parent->color == RED) {
    if (z->parent == z->parent->parent->left) {
      y = z->parent->parent->right;
      if (y->color == RED) {
        // case 1
        z->parent->color = BLACK;
        y->color = BLACK;
        z->parent->parent->color = RED;
        z = z->parent->parent;
      } else {
        if (z == z->parent->right) {
          // case 2
          z = z->parent;
          rotate_left(tree, z);
        }
        // case 3
        z->parent->color = BLACK;
        z->parent->parent->color = RED;
        rotate_right(tree, z->parent->parent);
      }
    } else {
      y = z->parent->parent->left;
      if (y->color == RED) {
        // case 4
        z->parent->color = BLACK;
        y->color = BLACK;
        z->parent->parent->color = RED;
        z = z->parent->parent;
      } else {
        if (z == z->parent->left) {
          // case 5
          z = z->parent;
          rotate_right(tree, z);
        }
        // case 6
        z->parent->color = BLACK;
        z->parent->parent->color = RED;
        rotate_left(tree, z->parent->parent);
      }
    }
  }
  tree->root->color = BLACK;
}

void insert_node(rb_tree_t *tree, rb_node_t *z) {
  callback(pre_insert_node, tree, z);

  rb_node_t *x = tree->root;
  rb_node_t *y = tree->nil;

  while (x != tree->nil) {
    y = x;
    if (z->key < x->key) {
      x = x->left;
    } else {
      x = x->right;
    }
  }

  z->parent = y;
  if (y == tree->nil) {
    tree->root = z;
  } else if (z->key < y->key) {
    y->left = z;
  } else {
    y->right = z;
  }

  z->color = RED;
  z->left = tree->nil;
  z->right = tree->nil;

  callback(post_insert_node, tree, z);

  // repair the tree
  insert_fixup(tree, z);
}

//
// Deletion
//

void delete_fixup(rb_tree_t *tree, rb_node_t *x) {
  rb_node_t *w;
  while (x != tree->root && x->color == BLACK) {
    if (x == x->parent->left) {
      w = x->parent->right;
      if (w->color == RED) {
        // case 1
        w->color = BLACK;
        x->parent->color = RED;
        rotate_left(tree, x->parent);
        w = x->parent->right;
      }

      if (w->left->color == BLACK && w->right->color == BLACK) {
        // case 2
        w->color = RED;
        x = x->parent;
      } else {
        if (w->right->color == BLACK) {
          // case 3
          w->left->color = BLACK;
          w->color = RED;
          rotate_right(tree, w);
          w = x->parent->right;
        }
        // case 4
        w->color = x->parent->color;
        x->parent->color = BLACK;
        w->right->color = BLACK;
        rotate_left(tree, x->parent);
        x = tree->root;
      }
    } else {
      w = x->parent->left;
      if (w->color == RED) {
        // case 5
        w->color = BLACK;
        x->parent->color = RED;
        rotate_right(tree, x->parent);
        w = x->parent->left;
      }

      if (w->right->color == BLACK && w->left->color == BLACK) {
        // case 6
        w->color = RED;
        x = x->parent;
      } else {
        if (w->left->color == BLACK) {
          // case 7
          w->right->color = BLACK;
          w->color = RED;
          rotate_left(tree, w);
          w = x->parent->left;
        }
        // case 8
        w->color = x->parent->color;
        x->parent->color = BLACK;
        w->left->color = BLACK;
        rotate_right(tree, x->parent);
        x = tree->root;
      }
    }
  }
  x->color = BLACK;
}

void delete_node(rb_tree_t *tree, rb_node_t *z) {
  callback(pre_delete_node, tree, z);

  rb_node_t *x;
  rb_node_t *y = z;
  rb_color_t orig_color = y->color;

  if (z->left == tree->nil) {
    x = z->right;
    replace_node(tree, z, z->right);
  } else if (z->right == tree->nil) {
    x = z->left;
    replace_node(tree, z, z->left);
  } else {
    y = minimum(tree, z->right);
    orig_color = y->color;
    x = y->right;
    if (y->parent == z) {
      x->parent = y;
    } else {
      replace_node(tree, y, y->right);
      y->right = z->right;
      y->right->parent = y;
    }

    replace_node(tree, z, y);
    y->left = z->left;
    y->left->parent = y;
    y->color = z->color;
  }

  callback(post_delete_node, tree, z, x);

  if (orig_color == BLACK) {
    // repair the tree
    delete_fixup(tree, x);
  }
}

//

rb_tree_t *create_rb_tree() {
  rb_node_t *nil = _malloc(sizeof(rb_node_t));
  nil->key = 0;
  nil->data = NULL;
  nil->color = BLACK;
  nil->left = nil;
  nil->right = nil;
  nil->parent = nil;

  rb_tree_t *tree = _malloc(sizeof(rb_tree_t));
  tree->root = nil;
  tree->nil = nil;
  tree->min = nil;
  tree->max = nil;
  tree->nodes = 0;
  tree->events = NULL;

  return tree;
}

rb_node_t *rb_tree_find(rb_tree_t *tree, uint64_t key) {
  rb_node_t *node = tree->root;
  while (node != tree->nil) {
    if (key == node->key) {
      return node;
    }

    if (key < node->key) {
      node = node->left;
    } else {
      node = node->right;
    }
  }
  return NULL;
}

rb_node_t *rb_tree_find_closest(rb_tree_t *tree, uint64_t key) {
  rb_node_t *closest = NULL;
  rb_node_t *node = tree->root;
  while (node != tree->nil) {
    uint64_t ldiff = udiff(node->left->key, key);
    uint64_t rdiff = udiff(node->right->key, key);

    closest = node;
    if (ldiff <= rdiff) {
      node = node->left;
    } else {
      node = node->right;
    }
  }

  if (closest == tree->nil) {
    return NULL;
  }
  return closest;
}

void rb_tree_insert(rb_tree_t *tree, uint64_t key, void *data) {
  rb_node_t *node = _malloc(sizeof(rb_node_t));
  node->key = key;
  node->data = data;
  node->color = RED;
  node->parent = tree->nil;
  node->left = tree->nil;
  node->right = tree->nil;
  rb_tree_insert_node(tree, node);
}

void rb_tree_insert_node(rb_tree_t *tree, rb_node_t *node) {
  insert_node(tree, node);
  if (tree->nodes == 0) {
    tree->min = node;
    tree->max = node;
  } else if (node->key < tree->min->key) {
    tree->min = node;
  } else if (node->key >= tree->max->key) {
    tree->max = node;
  }
  tree->nodes++;
}

void rb_tree_delete(rb_tree_t *tree, uint64_t key) {
  rb_node_t *node = rb_tree_find(tree, key);
  if (node == NULL) {
    return;
  }
  rb_tree_delete_node(tree, node);
}

void rb_tree_delete_node(rb_tree_t *tree, rb_node_t *node) {
  delete_node(tree, node);
  if (tree->nodes == 1) {
    tree->min = tree->nil;
    tree->max = tree->nil;
  } else if (node == tree->min) {
    if (tree->nodes == 2) {
      tree->min = tree->max;
    } else {
      tree->min = node->parent;
    }
  } else if (node == tree->max) {
    tree->max = node->parent;
  }
  tree->nodes--;
  _free(node->data);
  _free(node);
}

// Iterators

rb_iter_t *rb_tree_make_iter(rb_tree_t *tree, rb_node_t *next, rb_iter_type_t type) {
  rb_iter_t *iter = _malloc(sizeof(rb_iter_t));
  iter->type = type;
  iter->tree = tree;
  iter->next = next;
  iter->has_next = true;
  return iter;
}

rb_iter_t *rb_tree_iter(rb_tree_t *tree) {
  // start at first (leftmost) node
  return rb_tree_make_iter(tree, tree->min, FORWARD);
}

rb_iter_t *rb_tree_iter_reverse(rb_tree_t *tree) {
  // start at last (rightmost) node
  return rb_tree_make_iter(tree, tree->max, REVERSE);
}

rb_node_t *rb_iter_next(rb_iter_t *iter) {
  if (!iter->has_next) {
    return NULL;
  }

  rb_tree_t *tree = iter->tree;
  rb_node_t *node = iter->next;
  rb_node_t *next = iter->next;

  bool side = iter->type == REVERSE;
  if (get_side(next, side) != tree->nil) {
    next = get_side(next, side);
    while (get_side(next, !side) != tree->nil) {
      next = get_side(next, !side);
    }

    iter->next = next;
    iter->has_next = next != tree->nil;
    return node;
  }

  while (true) {
    if (next->parent == tree->nil) {
      iter->next = NULL;
      iter->has_next = false;
      return node;
    } else if (get_side(next->parent, !side) == next) {
      iter->next = next->parent;
      iter->has_next = next != tree->nil;
      return node;
    }
    next = next->parent;
  }
}

rb_node_t *rb_tree_get_next(rb_tree_t *tree, rb_node_t *node) {
  rb_iter_t *iter = rb_tree_make_iter(tree, node, FORWARD);
  rb_iter_next(iter); // skip node
  rb_node_t *result = rb_iter_next(iter);
  _free(iter);
  return result;
}

rb_node_t *rb_tree_get_last(rb_tree_t *tree, rb_node_t *node) {
  rb_iter_t *iter = rb_tree_make_iter(tree, node, REVERSE);
  rb_iter_next(iter); // skip node
  rb_node_t *result = rb_iter_next(iter);
  _free(iter);
  return result;
}
