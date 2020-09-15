//
// Created by Aaron Gill-Braun on 2020-09-06.
//

#ifndef LIB_SEGMENT_TREE_H
#define LIB_SEGMENT_TREE_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint32_t start;
  uint32_t end;
  void *data;
} interval_t;

typedef struct st_node {
  struct st_node *left;
  struct st_node *right;
  interval_t interval;
} st_node_t;

typedef struct {
  size_t size;
  st_node_t root;
} st_tree_t;

//



#endif
