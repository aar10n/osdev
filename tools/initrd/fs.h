//
// Created by Aaron Gill-Braun on 2020-09-08.
//

#ifndef INITRD_FS_H
#define INITRD_FS_H

#include <stdint.h>
#include <stdbool.h>

#include "initrd.h"

// Ramdisk in-memory filesystem

typedef struct {
  uint16_t last_id;
  uint16_t total_nodes;
} metadata_t;

typedef struct fs_node {
  uint16_t id;
  uint16_t flags;
  char name[MAX_NAME_LEN];
  struct fs_node *parent; // parent node
  struct fs_node *next;   // next node
  struct fs_node *prev;   // previous node
  union {
    // regular file
    struct {
      uint32_t length; // length of data
      uint8_t *buffer; // data buffer
    } file;
    // directory
    struct {
      struct fs_node *first; // first file in dir
      struct fs_node *last;  // last file in dir
    } dir;
    // symbolic link
    struct {
      struct fs_node *ptr;  // linked node
      uint32_t reserved;    // reserved
    } link;
  };
} fs_node_t;

typedef struct {
  metadata_t meta;
  fs_node_t *root;
} fs_t;

extern int fs_errno;
extern uint16_t iter_depth;

// get_node flags
#define GET_CREATE    0x1 // Create intermediate directories
#define GET_FILE      0x2 // Fail if the result is not a file
#define GET_DIRECTORY 0x4 // Fail if the result is not a directory
#define GET_NOFOLLOW  0x8 // Disable symbolic link resolution

fs_node_t *create_file(char *name, fs_node_t *parent, uint32_t length, uint8_t *buffer);
fs_node_t *create_directory(char *name, fs_node_t *parent, bool children);
fs_node_t *create_symlink(char *name, fs_node_t *parent, fs_node_t *link);

fs_node_t *next_node(fs_node_t *root, fs_node_t *node);
int get_node(fs_node_t *root, char *path, int flags, fs_node_t **result);

uint16_t get_tree_size(fs_node_t *root);
uint16_t get_tree_depth(fs_node_t *root);
uint16_t get_num_children(fs_node_t *parent);
char *get_node_path(fs_node_t *node);

void fs_lsdir(fs_node_t *root, char *path);
void fs_catfile(fs_node_t *root, char *path);

#endif
