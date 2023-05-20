//
// Created by Aaron Gill-Braun on 2023-05-14.
//

#include <ramfs/ramfs.h>

#include <mm.h>

struct sb_data {
  ino_t next_ino;
};


int ramfs_sb_mount(super_block_t *sb, dentry_t *mount) {
  struct sb_data *data = kmallocz(sizeof(struct sb_data));
  data->next_ino = 1;
  return 0;
}

int ramfs_sb_unmount(super_block_t *sb) {
  kfree(sb->data);
  return 0;
}

int ramfs_sb_write(super_block_t *sb) {
  return 0;
}

int ramfs_sb_read_inode(super_block_t *sb, inode_t *inode) {
  return 0;
}

int ramfs_sb_write_inode(super_block_t *sb, inode_t *inode) {
  return 0;
}

int ramfs_sb_alloc_inode(super_block_t *sb, inode_t *inode) {
  struct sb_data *data = sb->data;
  inode->ino = data->next_ino++;
  if (inode->ino == 0) {
    return -ENOSPC;
  }
  return 0;
}

int ramfs_sb_delete_inode(super_block_t *sb, inode_t *inode) {
  return 0;
}
