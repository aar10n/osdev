//
// Created by Aaron Gill-Braun on 2019-08-06.
//

#include <fs/fs.h>
#include <stddef.h>
#include <stdio.h>

#define MAX_FS_TYPES 4

inode_t *fs_root;
fs_type_t *types[MAX_FS_TYPES];

uint32_t next_id = 0;

//

void fs_register_type(fs_type_t *type) {
  kprintf("registering filesystem\n");
  if (next_id >= MAX_FS_TYPES) {
    return;
  }

  uint32_t id = next_id;
  next_id++;

  type->id = id;
  types[id] = type;

  kprintf("%s registered!\n", type->name);

  int result = type->mount(type);
  if (result == -1) {
    kprintf("failed to initialize %s\n", type->name);
    return;
  }

  kprintf("%s mounted!\n", type->name);
}

//

int fs_read(inode_t *root, uint32_t inode, inode_t *result) {
  if (root->fs && root->fs->read) {
    return root->fs->read(root, inode, result);
  }
  return -1;
}

int fs_write(inode_t *node) {
  if (node->fs && node->fs->write) {
    return node->fs->write(node);
  }
  return -1;
}

//

int fs_open(inode_t *node) {
  if (node->fs && node->fs->open) {
    return node->fs->open(node);
  }
  return -1;
}

int fs_close(inode_t *node) {
  if (node->fs && node->fs->close) {
    return node->fs->close(node);
  }
  return -1;
}

//

int fs_readdir(inode_t *node, int index, dirent_t *result) {
  if (!(node->mode & FS_DIRECTORY)) {
    kprintf("node is not a directory\n");
    return -1;
  }

  if (node->fs && node->fs->readdir) {
    return node->fs->readdir(node, index, result);
  }
  return -1;
}

int fs_finddir(inode_t *node, char *name, dirent_t *result) {
  if (!(node->mode & FS_DIRECTORY)) {
    kprintf("node is not a directory\n");
    return -1;
  }

  if (node->fs && node->fs->finddir) {
    return node->fs->finddir(node, name, result);
  }
  return -1;
}

//



// open(2)
// stat(2)
// read(2)
// write(2)
// chmod(2)

