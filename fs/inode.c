//
// Created by Aaron Gill-Braun on 2020-10-31.
//

#include <inode.h>
#include <fs.h>
#include <mm/heap.h>
#include <process.h>

#define UID (current_process->uid)
// #define UID (PERCPU->uid)

#define GID (current_process->gid)
// #define GID (PERCPU->gid)

inode_table_t *inodes;

inode_table_t *create_inode_table() {
  inode_table_t *inode_table = kmalloc(sizeof(inode_table_t));
  inode_table->inodes = create_rb_tree();
  spin_init(&inode_table->lock);
  return inode_table;
}

inode_t *inode_get(fs_node_t *node) {
  spin_lock(&inodes->lock);
  rb_node_t *rb_node = rb_tree_find(inodes->inodes, node->inode);
  spin_unlock(&inodes->lock);

  if (rb_node) {
    return rb_node->data;
  }

  // if the inode is not already cached we have
  // to load it from the nodes backing filesystem
  inode_t *inode = node->fs->impl->locate(node->fs, node->inode);
  if (inode) {
    spin_lock(&inodes->lock);
    rb_tree_insert(inodes->inodes, inode->ino, inode);
    spin_unlock(&inodes->lock);
  }
  return inode;
}

inode_t *inode_create(fs_t *fs, mode_t mode) {
  inode_t *inode = fs->impl->create(fs, mode);
  if (inode == NULL) {
    return NULL;
  }

  inode->dev = 0;
  inode->nlink = 0;
  inode->uid = UID;
  inode->gid = GID;
  inode->rdev = 0;

  inode->atime = 0;
  inode->ctime = 0;
  inode->mtime = 0;

  spin_init(&inode->lock);

  spin_lock(&inodes->lock);
  rb_tree_insert(inodes->inodes, inode->ino, inode);
  spin_unlock(&inodes->lock);

  return inode;
}

int inode_delete(fs_t *fs, inode_t *inode) {
  int result = fs->impl->remove(fs, inode);
  if (result < 0) {
    return -1;
  }

  spin_lock(&inodes->lock);
  rb_tree_delete(inodes->inodes, inode->ino);
  spin_unlock(&inodes->lock);
  return 0;
}
