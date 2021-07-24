//
// Created by Aaron Gill-Braun on 2021-07-16.
//

#include <super.h>
#include <inode.h>
#include <dentry.h>
#include <process.h>
#include <thread.h>
#include <panic.h>


// int mount_filesystem(file_system_t *type, blkdev_t);

inode_t *sb_alloc_inode(super_block_t *sb) {
  inode_t *inode = sb->ops->alloc_inode(sb);
  if (inode == NULL) {
    return NULL;
  }

  inode->nlink = 0;
  inode->blkdev = sb->dev;
  inode->pages = NULL;
  inode->sb = sb;
  inode->ops = sb->fs->inode_ops;
  rw_lock_init(&inode->lock);
  return inode;
}

int sb_destroy_inode(inode_t *inode) {
  return inode->sb->ops->destroy_inode(inode->sb, inode);
}

int sb_read_inode(dentry_t *dentry) {
  if (IS_LOADED(dentry->mode)) {
    return 0;
  }

  super_block_t *sb = dentry->parent->inode->sb;
  if (sb->inode_cache) {
    rb_node_t *node = rb_tree_find(sb->inode_cache, dentry->ino);
    if (node != NULL) {
      d_attach(dentry, node->data);
      return 0;
    }
  }

  inode_t *inode = dentry->inode;
  if (dentry->inode == NULL) {
    dentry_t *parent = dentry->parent;
    kassert(IS_LOADED(parent->mode));
    inode = i_alloc(dentry->ino, parent->inode->sb);
    dentry->inode = inode;
  }

  int result = inode->sb->ops->read_inode(inode->sb, inode);
  if (result < 0) {
    return -1;
  }

  if (sb->inode_cache) {
    rb_tree_insert(sb->inode_cache, dentry->ino, inode);
  }

  dentry->mode |= S_ISLDD | S_ISDTY;
  return 0;
}

int sb_write_inode(inode_t *inode) {
  if (!IS_DIRTY(inode->mode)) {
    return 0;
  }

  int result = inode->sb->ops->write_inode(inode->sb, inode);
  if (result < 0) {
    return -1;
  }

  inode->mode ^= S_ISDTY;
  return 0;
}
