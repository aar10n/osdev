//
// Created by Aaron Gill-Braun on 2020-11-03.
//

#ifndef FS_VFS_H
#define FS_VFS_H

#include <base.h>
#include <lock.h>
#include <hash_table.h>
#include <rb_tree.h>

typedef struct fs fs_t;
typedef struct fs_device fs_device_t;
typedef struct path path_t;
typedef struct inode inode_t;

// A node in the virtual filesystem
typedef struct fs_node {
  ino_t inode;  // inode
  dev_t dev;    // device
  mode_t mode;  // file mode
  char *name;   // file name
  fs_t *fs;     // containing filesystem
  struct fs_node *parent;
  struct fs_node *next;
  struct fs_node *prev;
  union {
    struct { // V_IFREG
    } ifreg;
    struct { // V_IFDIR
      struct fs_node *first;
      struct fs_node *last;
    } ifdir;
    struct { // V_IFBLK
      fs_device_t *device;
    } ifblk;
    struct { // V_IFSOCK
    } ifsock;
    struct { // V_IFLNK
      char *path;
    } iflnk;
    struct { // V_IFIFO
    } ififo;
    struct { // V_IFCHR
    } ifchr;
    struct { // V_IFMNT
      struct fs_node *shadow;
    } ifmnt;
  };
} fs_node_t;

typedef struct fs_node_table {
  map_t(fs_node_t *) hash_table;
  rw_spinlock_t rwlock;
} fs_node_table_t;

typedef struct fs_node_map {
  rb_tree_t *tree;
  rw_spinlock_t rwlock;
} fs_node_map_t;

extern fs_node_t *fs_root;
extern fs_node_map_t *nodes;
extern fs_node_table_t *links;


fs_node_table_t *create_node_table();
fs_node_map_t *create_node_map();

void vfs_init();
fs_node_t *vfs_create_node();
fs_node_t *vfs_create_node_from_inode(inode_t *inode);

fs_node_t *vfs_get_node(path_t path, int flags);
fs_node_t *vfs_find_child(fs_node_t *parent, path_t name);
fs_node_t *vfs_resolve_link(fs_node_t *node, int flags);
void vfs_add_device(fs_device_t *device);
void vfs_add_node(fs_node_t *parent, fs_node_t *child);
void vfs_remove_node(fs_node_t *node);
void vfs_swap_node(fs_node_t *orig_node, fs_node_t *new_node);

char *vfs_path_from_node(fs_node_t *node);

#endif
