//
// Created by Aaron Gill-Braun on 2019-04-22.
//

#ifndef INCLUDE_FS_EXT2_EXT2_H
#define INCLUDE_FS_EXT2_EXT2_H

#include <stdint.h>
#include <fs/ext2/ext2.h>
#include <fs/ext2/inode.h>
#include <fs/ext2/directory.h>
#include <fs/ext2/superblock.h>

typedef struct __attribute__((packed)) {
  uint8_t boot_sector[1024];    // 1024 Bytes
  superblock_t superblock;      // 1 Block
  bg_descriptor_table_t bgdt;   // 1 Block
  uint8_t block_bitmap[1024];   // 1 Block
  uint8_t inode_bitmap[1024];   // 1 Block
  inode_table_t inode_table;    // 4 Blocks
  uint8_t data_blocks[8192];    // 8 Blocks
} fs_root_t;

void create_root(fs_root_t *root);
void add_directory(fs_root_t *root);

#endif //INCLUDE_FS_EXT2_EXT2_H
