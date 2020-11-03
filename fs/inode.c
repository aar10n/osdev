//
// Created by Aaron Gill-Braun on 2020-10-31.
//

#include <inode.h>
#include <mm/heap.h>

void __init_inode(inode_t *inode, ino_t ino, dev_t dev, mode_t mode) {
  inode->ino = ino;
  inode->dev = dev;
  inode->mode = mode;

  inode->nlink = 0;
  inode->uid = 0;
  inode->gid = 0;
  inode->rdev = 0;
  inode->size = 0;

  inode->atime = 0;
  inode->ctime = 0;
  inode->mtime = 0;

  inode->blksize = 0;
  inode->blocks = 0;

  spin_init(&inode->lock);

  inode->name = NULL;
  inode->impl = NULL;
}

inode_t *__create_inode(ino_t ino, dev_t dev, mode_t mode) {
  inode_t *inode = kmalloc(sizeof(inode_t));
  __init_inode(inode, ino, dev, mode);
  return inode;
}

inode_table_t *__create_inode_table() {
  inode_table_t *inode_table = kmalloc(sizeof(inode_table_t));
  inode_table->inodes = create_rb_tree();
  spin_init(&inode_table->lock);
  return inode_table;
}

inode_t *__fetch_inode(inode_table_t *table, ino_t ino) {
  lock(table->lock);
  rb_node_t *node = rb_tree_find(table->inodes, ino);
  unlock(table->lock);
  return node->data;
}

void __cache_inode(inode_table_t *table, ino_t ino, inode_t *inode) {
  lock(table->lock);
  rb_tree_insert(table->inodes, ino, inode);
  unlock(table->lock);
}

void __remove_inode(inode_table_t *table, ino_t ino) {
  lock(table->lock);
  rb_tree_delete(table->inodes, ino);
  unlock(table->lock);
}


