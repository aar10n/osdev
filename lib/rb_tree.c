//
// Created by Aaron Gill-Braun on 2020-10-12.
//

#include "rb_tree.h"

#include <kernel/panic.h>
#define _assert(expr) kassert((expr))

#include <kernel/mm/heap.h>
#include <kernel/printf.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("rb_tree: " fmt, ##__VA_ARGS__)

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

// Insertion

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

  // find the insertion point
  while (x != tree->nil) {
    y = x;
    if (z->key < x->key) {
      x = x->left;
    } else {
      x = x->right;
    }
  }

  // insert the node
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

  // handle linked list pointers
  if (tree->nodes == 0) {
    // first node
    z->prev = tree->nil;
    z->next = tree->nil;
    tree->min = z;
    tree->max = z;
  } else {
    // find correct position in linked list
    rb_node_t *curr = tree->min;
    while (curr != tree->nil && curr->key < z->key) {
      curr = curr->next;
    }

    if (curr == tree->nil) {
      // insert at end
      z->prev = tree->max;
      z->next = tree->nil;
      tree->max->next = z;
      tree->max = z;
    } else if (curr == tree->min) {
      // insert at beginning
      z->next = tree->min;
      z->prev = tree->nil;
      tree->min->prev = z;
      tree->min = z;
    } else {
      // insert between nodes
      z->next = curr;
      z->prev = curr->prev;
      curr->prev->next = z;
      curr->prev = z;
    }
  }

  // repair the tree
  insert_fixup(tree, z);
  tree->nodes++;

  callback(post_insert_node, tree, z);
}

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

  // update linked list pointers
  if (z->prev != tree->nil) {
    z->prev->next = z->next;
  } else {
    tree->min = z->next;
  }

  if (z->next != tree->nil) {
    z->next->prev = z->prev;
  } else {
    tree->max = z->prev;
  }

  rb_node_t *x;
  rb_node_t *y = z;
  enum rb_color orig_color = y->color;

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

  tree->nodes--;
  callback(post_delete_node, tree, z, x);

  if (orig_color == BLACK) {
    // repair the tree
    delete_fixup(tree, x);
  }
}

void fix_list_pointers(rb_tree_t *tree) {
  rb_node_t *curr = tree->min;
  rb_node_t *prev = tree->nil;

  while (curr != tree->nil) {
    rb_node_t *next = NULL;
    rb_node_t *node = tree->root;

    // find next node in order
    uint64_t min_key = UINT64_MAX;
    rb_node_t *min_node = NULL;

    // find the smallest key larger than current
    while (node != tree->nil) {
      if (node->key > curr->key && node->key < min_key && node != curr) {
        min_key = node->key;
        min_node = node;
      }
      if (curr->key < node->key) {
        node = node->left;
      } else {
        node = node->right;
      }
    }

    next = min_node ? min_node : tree->nil;

    // set up doubly-linked pointers
    curr->prev = prev;
    curr->next = next;
    if (prev != tree->nil) {
      prev->next = curr;
    }

    prev = curr;
    curr = next;
  }
}

//

rb_node_t *duplicate_node(rb_tree_t *tree, rb_tree_t *new_tree, rb_node_t *parent, rb_node_t *node) {
  if (node == tree->nil) {
    return new_tree->nil;
  }

  rb_node_t *copy = kmalloc(sizeof(rb_node_t));
  copy->key = node->key;
  copy->data = NULL;
  copy->color = node->color;
  copy->left = duplicate_node(tree, new_tree, copy, node->left);
  copy->right = duplicate_node(tree, new_tree, copy, node->right);
  copy->parent = parent;
  copy->next = new_tree->nil;
  copy->prev = new_tree->nil;
  new_tree->nodes++;

  if (tree->events && tree->events->duplicate_node) {
    callback(duplicate_node, tree, new_tree, node, copy);
  } else {
    copy->data = node->data;
  }

  if (node == tree->min) {
    new_tree->min = copy;
  }
  if (node == tree->max) {
    new_tree->max = copy;
  }
  return copy;
}


//

rb_tree_t *create_rb_tree() {
  rb_node_t *nil = kmalloc(sizeof(rb_node_t));
  nil->key = 0;
  nil->data = NULL;
  nil->color = BLACK;
  nil->left = nil;
  nil->right = nil;
  nil->parent = nil;

  rb_tree_t *tree = kmalloc(sizeof(rb_tree_t));
  tree->root = nil;
  tree->nil = nil;
  tree->min = nil;
  tree->max = nil;
  tree->nodes = 0;
  tree->events = NULL;

  return tree;
}

void rb_tree_free(rb_tree_t *tree) {
  ASSERT(tree->root == tree->nil);
  kfree(tree->nil);
  kfree(tree);
}

rb_tree_t *copy_rb_tree(rb_tree_t *tree) {
  rb_tree_t *new_tree = create_rb_tree();
  new_tree->events = tree->events;
  new_tree->root = duplicate_node(tree, new_tree, tree->nil, tree->root);
  fix_list_pointers(new_tree);
  return new_tree;
}

//

rb_node_t *rb_tree_find_node(rb_tree_t *tree, uint64_t key) {
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
  return tree->nil;
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

void *rb_tree_delete_node(rb_tree_t *tree, rb_node_t *node) {
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

  void *data = node->data;
  kfree(node);
  return data;
}

//

void *rb_tree_find(rb_tree_t *tree, uint64_t key) {
  rb_node_t *node = rb_tree_find_node(tree, key);
  if (rb_node_is_nil(node)) {
    return NULL;
  }
  return node->data;
}

void rb_tree_insert(rb_tree_t *tree, uint64_t key, void *data) {
  rb_node_t *node = kmalloc(sizeof(rb_node_t));
  node->key = key;
  node->data = data;
  node->color = RED;
  node->parent = tree->nil;
  node->left = tree->nil;
  node->right = tree->nil;
  rb_tree_insert_node(tree, node);
}

void *rb_tree_delete(rb_tree_t *tree, uint64_t key) {
  rb_node_t *node = rb_tree_find_node(tree, key);
  if (rb_node_is_nil(node)) {
    return NULL;
  }
  return rb_tree_delete_node(tree, node);
}
