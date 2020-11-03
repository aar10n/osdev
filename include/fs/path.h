//
// Created by Aaron Gill-Braun on 2020-11-01.
//

#ifndef FS_PATH_H
#define FS_PATH_H

#include <base.h>
#include <fs.h>

typedef struct {
  const char *path;
  char *ptr;
  char *last;
} path_iter_t;

#define path_iter(path) \
  ((path_iter_t){ path, (char *) path, NULL })

char *path_basename(const char *path);
char *path_dirname(const char *path);
char *path_iter_next(path_iter_t *iter);

fs_node_t *path_resolve_link(fs_node_t *node, int flags);
fs_node_t *path_get_node(fs_node_t *root, const char *path, int flags);
char *path_from_node(fs_node_t *node);

#endif
