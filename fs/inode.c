//
// Created by Aaron Gill-Braun on 2020-10-31.
//

#include <inode.h>
#include <process.h>
#include <fs.h>
#include <mm.h>
#include <printf.h>

#define UID (current_process->uid)
#define GID (current_process->gid)

inode_table_t *inodes;

inode_table_t *create_inode_table() {
  inode_table_t *inode_table = kmalloc(sizeof(inode_table_t));
  inode_table->inodes = create_rb_tree();
  mutex_init(&inode_table->lock, 0);
  return inode_table;
}

inode_t *inode_get(fs_node_t *node) {
  mutex_lock(&inodes->lock);
  rb_node_t *rb_node = rb_tree_find(inodes->inodes, node->inode);
  mutex_unlock(&inodes->lock);

  if (rb_node != NULL) {
    return rb_node->data;
  }

  inode_t *parent = NULL;
  if (node->parent && !IS_IFMNT(node->parent->mode)) {
    mutex_lock(&inodes->lock);
    rb_node = rb_tree_find(inodes->inodes, node->parent->inode);
    mutex_unlock(&inodes->lock);
    if (rb_node == NULL) {
      parent = inode_get(node->parent);
    } else {
      parent = rb_node->data;
    }
  }

  // if the inode is not already cached we have
  // to load it from the backing filesystem
  inode_t *inode = node->fs->impl->locate(node->fs, parent, node->inode);
  if (inode) {
    mutex_lock(&inodes->lock);
    rb_tree_insert(inodes->inodes, inode->ino, inode);
    mutex_unlock(&inodes->lock);
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

  mutex_init(&inode->lock, 0);
  mutex_lock(&inodes->lock);
  rb_tree_insert(inodes->inodes, inode->ino, inode);
  mutex_unlock(&inodes->lock);

  return inode;
}

void inode_insert(inode_t *inode) {
  if (inode == NULL) {
    return;
  }

  mutex_init(&inode->lock, 0);
  mutex_lock(&inodes->lock);
  rb_tree_insert(inodes->inodes, inode->ino, inode);
  mutex_unlock(&inodes->lock);
}

int inode_delete(fs_t *fs, inode_t *inode) {
  int result = fs->impl->remove(fs, inode);
  if (result < 0) {
    return -1;
  }

  mutex_lock(&inodes->lock);
  rb_tree_delete(inodes->inodes, inode->ino);
  mutex_unlock(&inodes->lock);
  return 0;
}

void inode_remove(inode_t *inode) {
  if (inode == NULL) {
    return;
  }

  mutex_lock(&inodes->lock);
  rb_tree_delete(inodes->inodes, inode->ino);
  mutex_unlock(&inodes->lock);
  kfree(inode);
}
