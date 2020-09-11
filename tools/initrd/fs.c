//
// Created by Aaron Gill-Braun on 2020-09-08.
//

#include <libgen.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "fs.h"
#include "path.h"

int fs_errno = 0;
uint16_t last_id = 0;
uint16_t iter_depth = 0;
fs_node_t *iter_last = NULL;

/* ----- Node Creation ----- */

fs_node_t *create_node(char *name, fs_node_t *parent) {
  fs_node_t *node = malloc(sizeof(fs_node_t));
  memset(node, 0, sizeof(fs_node_t));
  node->id = last_id++;
  size_t name_len = strlen(name);
  memcpy(node->name, name, fmin(MAX_NAME_LEN - 1, name_len + 1));
  node->parent = parent;

  if (parent && parent->dir.last) {
    assert(parent->flags & FILE_DIRECTORY);
    fs_node_t *last = parent->dir.last;
    last->next = node;
    node->prev = last;

    parent->dir.last = node;
  }
  return node;
}

fs_node_t *create_file(char *name, fs_node_t *parent, uint32_t length, uint8_t *buffer) {
  fs_node_t *node = create_node(name, parent);
  node->flags = FILE_REGULAR;
  node->file.length = length;
  node->file.buffer = buffer;
  return node;
}

fs_node_t *create_directory(char *name, fs_node_t *parent, bool children) {
  fs_node_t *node = create_node(name, parent);
  node->flags = FILE_DIRECTORY;

  // create self and parent links
  if (children) {
    fs_node_t *parent_link = create_symlink("..", node, parent ? parent : node);
    fs_node_t *self_link = create_symlink(".", node, node);
    // add them to directory explicitly
    self_link->next = parent_link;
    parent_link->prev = self_link;

    node->dir.first = self_link;
    node->dir.last = parent_link;
  }

  return node;
}

fs_node_t *create_symlink(char *name, fs_node_t *parent, fs_node_t *link) {
  fs_node_t *node = create_node(name, parent);
  node->flags = FILE_SYMLINK;
  node->link.ptr = link;
  return node;
}

/* ----- Node Traversal ----- */

fs_node_t *next_node(fs_node_t *root, fs_node_t *node) {
  if (!node) {
    // this is the first iteration
    iter_depth = 0;
    return root;
  }

  // step down a directory
  if (node->flags & FILE_DIRECTORY) {
    iter_depth++;
    return node->dir.first;
  }

  fs_node_t *next = node->next;
  while (!next) {
    // walk back up
    if (node->parent == NULL) {
      return NULL;
    }

    iter_depth--;
    next = node->parent->next;
    node = node->parent;
  }

  return next;
}

int get_node(fs_node_t *root, char *path, int flags, fs_node_t **result) {
  assert(root->flags & FILE_DIRECTORY);

  char **parts;
  int n = split_path(path, &parts);

  fs_node_t *node = root;
  fs_node_t *parent = root->parent;
  for (int i = 0; i < n; i++) {
    if (node == root) {
      node = root->dir.first;
      parent = root;
    }

    char *part = parts[i];
    while (node) {
      // look for the node matching the path part
      if (strcmp(node->name, part) == 0) {
        if (node->flags & FILE_REGULAR) {
          if (i < n - 1) {
            *result = node;
            fs_errno = ENOTDIR;
            return -1;
          }
          goto outer;
        } else if (node->flags & FILE_DIRECTORY) {
          if (i == n - 1) {
            goto outer;
          }

          // go down into next directory
          parent = node;
          node = node->dir.first;
          goto outer;
        } else if (node->flags & FILE_SYMLINK) {
          if (flags & GET_NOFOLLOW) {
            goto outer;
          }

          // resolve any and all symlinks
          fs_node_t *tmp = node;
          int link_count = 0;
          while (tmp->flags & FILE_SYMLINK) {
            if (link_count > MAX_SYMLINKS) {
              *result = node;
              fs_errno = ELOOP;
              return -1;
            }

            tmp = tmp->link.ptr;
            link_count++;
          }

          parent = tmp->parent;
          node = tmp;
          goto outer;
        }

        fprintf(stderr, "initrd: an internal error occured\n");
        exit(1);
      }

      // check the next node
      node = node->next;
    }

    // no node was found
    if (flags & GET_CREATE) {
      // create the intermediate directories
      fs_node_t *new_node = create_directory(part, parent, true);

      char *new_path = get_node_path(new_node);
      print(VERBOSE, "creating directory %s\n", new_path);
      free(new_path);

      if (i < n - 1) {
        parent = new_node;
        node = new_node->dir.first;
      } else {
        node = new_node;
      }
    } else {
      *result = node;
      fs_errno = ENOENT;
      return -1;
    }

  outer: NULL;
  }

  *result = node;
  free_split(parts, n);
  if (flags & GET_FILE) {
    if (node->flags & FILE_DIRECTORY) {
      fs_errno = EISDIR;
      return -1;
    }
    return 0;
  } else if (flags & GET_DIRECTORY) {
    if (node->flags & FILE_REGULAR) {
      fs_errno = ENOTDIR;
      return -1;
    }
    return 0;
  }

  return 0;
}

/* ----- Node Information ----- */

uint16_t get_tree_size(fs_node_t *root) {
  assert(root->flags & FILE_DIRECTORY);

  uint16_t size = 0;
  fs_node_t *node = NULL;
  print(VERBOSE, "\n      nodes      \n");
  print(VERBOSE, "-----------------\n");
  while ((node = next_node(root, node))) {
    print(VERBOSE, "node %02d | %s\n", node->id, node->name);
    size++;
  }
  print(VERBOSE, "-----------------\n");
  print(VERBOSE, "\n");

  return size;
}

uint16_t get_tree_depth(fs_node_t *root) {
  assert(root->flags & FILE_DIRECTORY);

  uint16_t depth = 0;
  fs_node_t *node = NULL;
  while ((node = next_node(root, node))) {
    if (iter_depth > depth) {
      depth = iter_depth;
    }
  }
  return depth;
}

uint16_t get_num_children(fs_node_t *parent) {
  assert(parent->flags & FILE_DIRECTORY);

  uint16_t size = 0;
  fs_node_t *node = parent->dir.first;
  while (node) {
    node = node->next;
    size++;
  }
  return size;
}

char *get_node_path(fs_node_t *node) {
  uint32_t next_len;

  uint32_t buf_size = 32;
  char *path1 = malloc(buf_size);
  char *path2 = malloc(buf_size);
  memset(path1, 0, buf_size);
  memset(path2, 0, buf_size);

  bool alt = false;
  while (node) {
    int n;
    if (node->parent == NULL) {
      // we've reached the root node
      n = snprintf(alt ? path2 : path1, buf_size, "/%s", alt ? path1 : path2);
      next_len = 1;
    } else {
      n = snprintf(alt ? path2 : path1, buf_size, "/%s%s", node->name, alt ? path1 : path2);
      next_len = strlen(node->name) + 1;
    }

    if (n < 0 || (uint32_t) n >= buf_size) {
      // expand the buffer and try joining the path again
      buf_size *= 2;
      path1 = realloc(path1, buf_size);
      path2 = realloc(path2, buf_size);
      path1[next_len] = '\0';
      path2[next_len] = '\0';
      continue;
    }

    alt = !alt;
    node = node->parent;
  }

  free(alt ? path1 : path2);
  return alt ? path2 : path1;
}

/* ----- Filesystem Operations ----- */

void fs_lsdir(fs_node_t *root, char *path) {
  fs_node_t *node;
  if (get_node(root, path, 0, &node) == -1) {
    fprintf(stderr, "initrd: %s: %s\n", path, strerror(fs_errno));
    exit(1);
  }

  if (node->flags & FILE_REGULAR) {
    // copy behavior of posix ls which is to print the name of
    // the file if the path does not point to a directory
    print(QUIET, "%s\n", node->name);
  } else if (node->flags & FILE_DIRECTORY) {
    // otherwise print each entry in the directory
    fs_node_t *child = node->dir.first;
    while (child) {
      print(QUIET, "%s ", child->name);
      child = child->next;
    }
    print(QUIET, "\n");
  }
}

void fs_catfile(fs_node_t *root, char *path) {
  fs_node_t *node;
  if (get_node(root, path, GET_FILE, &node) == -1) {
    fprintf(stderr, "initrd: %s: %s\n", path, strerror(fs_errno));
    exit(1);
  }

  // it is a file
  uint8_t *file = node->file.buffer;
  for (uint32_t i = 0; i < node->file.length; i++) {
    putchar(*(file + i));
  }
}
