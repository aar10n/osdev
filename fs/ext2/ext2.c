//
// Created by Aaron Gill-Braun on 2019-04-22.
//

// ext2 Filesystem Implementation

#include <stdint.h>
#include <fs/ext2/ext2.h>
#include <fs/ext2/inode.h>
#include <fs/ext2/directory.h>
#include <fs/ext2/superblock.h>
#include <string.h>

void create_root(fs_root_t *root) {
  root->superblock.s_magic = EXT2_SUPER_MAGIC;
  root->superblock.s_inodes_count = 1;
  root->superblock.s_blocks_count = 17;
  root->superblock.s_inode_size = sizeof(inode_t);
  root->superblock.s_inodes_per_group = 1024 / sizeof(inode_t);

  memset(root->block_bitmap, 0xFF, 1024);
  memset(root->inode_bitmap, 0xFF, 1024);

  root->inode_table[0].i_mode = EXT2_S_IFDIR;
  root->inode_table[0].i_uid = 0xABCD;
  root->inode_table[0].i_blocks = 16;
  root->inode_table[0].i_flags = EXT2_NODUMP_FL;

  root->inode_table[0].i_block[0] = 9;
  root->inode_table[0].i_block[1] = 10;
  root->inode_table[0].i_block[2] = 11;
  root->inode_table[0].i_block[3] = 12;
}

void add_directory(fs_root_t *root) {
  linked_directory_entry_t lde = {
      0, // inode
      0, // rec_len
      1, // name_len
      EXT2_FT_DIR, // file_type
      "/" // name
  };
  lde.rec_len = (uint16_t )(sizeof(lde) << 8);

  uint32_t loc = (9 - root->inode_table[0].i_block[0]) * 1024;
  memcpy(root->data_blocks + loc, &lde, lde.rec_len);
}
