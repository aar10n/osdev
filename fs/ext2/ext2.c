//
// Created by Aaron Gill-Braun on 2019-04-22.
//

// ext2 Filesystem Driver

#include <fs/ext2/ext2.h>
#include "../../libc/stdio.h"
#include "../../libc/string.h"
#include "../../kernel/mem/heap.h"

void ext2_mount(ata_t *disk) {
  superblock_t super;
  bg_descriptor_table_t bg_descriptor_table;

  ata_read(disk, 2, 2, (uint8_t *) &super);
  ata_read(disk, 4, 2, (uint8_t *) &bg_descriptor_table);

  int inodes_per_block = (1024 << super.s_log_block_size) / super.s_inode_size;
  int inode_block_count = super.s_inodes_per_group / inodes_per_block;

  inode_t inode_table[inode_block_count * 1024];
  ata_read(disk, 10, inode_block_count * 2, (uint8_t *) inode_table);

  inode_t root = inode_table[1];
  int block_count = root.i_blocks / (2 << super.s_log_block_size);
  uint8_t root_data[block_count * 1024];
  for (int i = 0; i < block_count; i++) {
    ata_read(disk, root.i_block[i] * 2, 2, (uint8_t *) (&root_data + (i * 1024)));
  }

  int index = 0;
  while (1) {
    linked_directory_entry_t *entry;
    entry = _kmalloc(sizeof(linked_directory_entry_t));

    size_t len = sizeof(linked_directory_entry_t) - sizeof(uint8_t *);
    memcpy(entry, (root_data + index), len);
    if (entry->rec_len == 0) {
      break;
    }

    char *name = _kmalloc(entry->name_len);
    memcpy(name, (root_data + index + len), entry->name_len);
    entry->name = name;

    kprintf("%s\n", entry->name);

    inode_t inode = inode_table[entry->inode - 1];
    if ((inode.i_mode & 0xF000) == EXT2_S_IFDIR) {

    }

    index += entry->rec_len;
  }

}

inode_t *ext2_alloc_node(uint16_t mode, uint16_t flags) {}

void ext2_destroy_node(inode_t *node) {}

void ext2_create_node(inode_t *node, ata_t *disk) {}

void ext2_remove_node(inode_t *node, ata_t *disk) {}

void ext2_read_data(inode_t *node, char *buffer) {}

void ext2_write_data(inode_t *node, char *buffer) {}

void ext2_mkdir(inode_t *parent, char *name) {}

void ext2_mkfile(inode_t *parent, char *name) {}

void ext2_rmdir(inode_t *parent, char *name) {}

void ext2_rmfile(inode_t *parent, char *name) {}
