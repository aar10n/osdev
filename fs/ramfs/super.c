//
// Created by Aaron Gill-Braun on 2021-07-17.
//

#include <ramfs/ramfs.h>
#include <inode.h>
#include <thread.h>

#define ramfs_sb(sb) ((ramfs_super_t *) (sb)->data)

inode_t *ramfs_alloc_inode(super_block_t *sb) {
  spin_lock(&ramfs_sb(sb)->lock);
  index_t ino = bitmap_get_set_free(&ramfs_sb(sb)->inodes);
  spin_unlock(&ramfs_sb(sb)->lock);
  if (ino < 0) {
    ERRNO = ENOSPC;
    return NULL;
  }

  inode_t *inode = i_alloc(ino, sb);
  inode->mode = S_ISLDD;
  inode->sb = sb;
  return inode;
}

int ramfs_destroy_inode(super_block_t *sb, inode_t *inode) {
  spin_lock(&ramfs_sb(sb)->lock);
  bitmap_clear(&ramfs_sb(sb)->inodes, inode->ino);
  spin_unlock(&ramfs_sb(sb)->lock);
  return 0;
}

int ramfs_read_inode(super_block_t *sb, inode_t *inode) {
  inode->mode |= S_ISLDD;
  return 0;
}

int ramfs_write_inode(super_block_t *sb, inode_t *inode) {
  return 0;
}

//

super_block_ops_t super_ops = {
  ramfs_alloc_inode,
  ramfs_destroy_inode,
  ramfs_read_inode,
  ramfs_write_inode,
};

super_block_ops_t *ramfs_super_ops = &super_ops;
