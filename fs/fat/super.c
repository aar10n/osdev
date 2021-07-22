//
// Created by Aaron Gill-Braun on 2021-07-22.
//

#include <fat/fat.h>
#include <inode.h>

//

inode_t *fat_alloc_inode(super_block_t *sb) {
  fat_super_t *fsb = sb->data;

}

int fat_destroy_inode(super_block_t *sb, inode_t *inode) {
  fat_super_t *fsb = sb->data;
}

int fat_read_inode(super_block_t *sb, inode_t *inode) {
  fat_super_t *fsb = sb->data;
}

int fat_write_inode(super_block_t *sb, inode_t *inode) {
  fat_super_t *fsb = sb->data;
}
