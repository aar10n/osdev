//
// Created by Aaron Gill-Braun on 2019-04-22.
//

// ext2 Filesystem Driver

#include <fs/ext2/ext2.h>
#include <kernel/mem/heap.h>

#include <fs/fs.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "dir.h"
#include "inode.h"
#include "super.h"

//

static ata_t disk = ATA_DRIVE_PRIMARY;

//

uint32_t get_fs_mode(ext2_inode_t *node) {
  uint32_t mode = 0;
  if (node->i_mode & EXT2_S_IFREG) {
    mode |= FS_FILE;
  } else if (node->i_mode & EXT2_S_IFDIR) {
    mode |= FS_DIRECTORY;
  } else if (node->i_mode & EXT2_S_IFCHR) {
    mode |= FS_CHARDEV;
  } else if (node->i_mode & EXT2_S_IFBLK) {
    mode |= FS_BLOCKDEV;
  } else if (node->i_mode & EXT2_S_IFIFO) {
    mode |= FS_FIFO;
  } else if (node->i_mode & EXT2_S_IFSOCK) {
    mode |= FS_SOCKET;
  } else if (node->i_mode & EXT2_S_IFLNK) {
    mode |= FS_SYMLINK;
  }
  return mode;
}

void convert_inode(uint32_t inode, ext2_inode_t *node, inode_t *fs_node) {
  fs_node->inode = inode;
  fs_node->mode = get_fs_mode(node);
  fs_node->perms = 0;
  fs_node->uid = node->i_uid;
  fs_node->gid = node->i_gid;
  fs_node->size = node->i_size;
  fs_node->links = node->i_links_count;
  fs_node->ctime = node->i_ctime;
  fs_node->atime = node->i_atime;
  fs_node->mtime = node->i_mtime;
}

//

int ext2_mount(fs_type_t *fs_type) {
  ext2_superblock_t *super = kmalloc(sizeof(ext2_superblock_t));
  ext2_bg_descriptor_t *bg_descriptor_table = kmalloc(sizeof(ext2_bg_descriptor_t));

  // superblock at 2 block offset from start of drive
  ata_read(&disk, 2, 2, (uint8_t *) super);
  // descriptor table comes right after superblock
  ata_read(&disk, 4, 2, (uint8_t *) bg_descriptor_table);

  kprintf("--- ext2 superblock ---\n");
  ext2_print_debug_super(super);

  uint32_t inodes_per_block = (1024 << super->s_log_block_size) / super->s_inode_size;
  uint32_t inode_block_count = super->s_inodes_per_group / inodes_per_block;

  super_t *fs_super = kmalloc(sizeof(super_t));
  fs_super->block_size = 1024 << super->s_log_block_size;
  fs_super->inode_size = super->s_inode_size;
  fs_super->inode_count = super->s_inodes_count;
  fs_super->block_count = super->s_blocks_count;
  fs_super->rblock_count = super->s_r_blocks_count;
  fs_super->free_block_count = super->s_free_blocks_count;
  fs_super->free_inode_count = super->s_free_inodes_count;
  fs_super->blocks_per_group = super->s_blocks_per_group;
  fs_super->inodes_per_group = super->s_inodes_per_group;
  fs_super->inodes_per_block = inodes_per_block;
  fs_super->first_inode = super->s_first_ino;
  strcpy(fs_super->name, (char *) super->s_volume_name);
  fs_type->super = fs_super;

  size_t order = log2((sizeof(ext2_inode_t) * inode_block_count * 1024) / PAGE_SIZE);
  page_t *page = alloc_pages(order, 0);
  ext2_inode_t *inode_table = (ext2_inode_t *) page->virt_addr;
  ata_read(&disk, 10, (int) inode_block_count * 2, (uint8_t *) inode_table);

  ext2_inode_t root = inode_table[1];

  inode_t *fs_root = kmalloc(sizeof(inode_t));
  convert_inode(1, &root, fs_root);
  fs_root->fs = fs_type;
  fs_root->mode |= FS_MOUNT;
  fs_type->root = fs_root;

  // uint32_t block_count = root.i_blocks / (2 << super->s_log_block_size);
  // uint8_t root_data[block_count * 1024];
  // // read in all the data blocks
  // for (int i = 0; i < block_count; i++) {
  //   ata_read(&disk, root.i_block[i] * 2, 2, (uint8_t *) (&root_data + (i * 1024)));
  // }

  return 0;
}

int ext2_inode_read(inode_t *root, uint32_t inode, inode_t *result) {

}


void ext2_lsdir(ext2_inode_t *node) {
  // int index = 0;
  // while (1) {
  //   ext2_dirent_t *entry;
  //   entry = kmalloc(sizeof(ext2_dirent_t));
  //
  //   size_t len = sizeof(ext2_dirent_t) - sizeof(uint8_t *);
  //   memcpy(entry, (root_data + index), len);
  //   if (entry->rec_len == 0) {
  //     break;
  //   }
  //
  //   char *name = kmalloc(entry->name_len);
  //   memcpy(name, (root_data + index + len), entry->name_len);
  //   entry->name = name;
  //
  //   // ext2_print_debug_dirent(entry);
  //
  //   kprintf("%s\n", entry->name);
  //
  //   ext2_inode_t inode = inode_table[entry->inode];
  //   if ((inode.i_mode & 0xF000) == EXT2_S_IFDIR) {
  //
  //   }
  //
  //
  //   if (index + entry->rec_len == 1024) {
  //     break;
  //   }
  //
  //   index += entry->rec_len;
  // }
}

// Debugging

void ext2_print_debug_super(ext2_superblock_t *super) {
  kprintf("super = {"
          "  inodes_count = %d\n"
          "  blocks_count = %d\n"
          "  r_blocks_count = %d\n"
          "  free_blocks_count = %d\n"
          "  free_inodes_count = %d\n"
          "  first_data_block = %d\n"
          "  log_block_size = %d\n"
          "  log_frag_size = %d\n"
          "  blocks_per_group = %d\n"
          "  frags_per_group = %d\n"
          "  inodes_per_group = %d\n"
          "}\n",
          super->s_inodes_count,
          super->s_blocks_count,
          super->s_r_blocks_count,
          super->s_free_blocks_count,
          super->s_free_inodes_count,
          super->s_first_data_block,
          super->s_log_block_size,
          super->s_log_frag_size,
          super->s_blocks_per_group,
          super->s_frags_per_group,
          super->s_inodes_per_group);
}

void ext2_print_debug_dirent(ext2_dirent_t *dirent) {
  kprintf("dirent = {\n"
          "  inode = %d\n"
          "  rec_len = %d\n"
          "  name_len = %d\n"
          "  file_type = %b\n"
          "  name = \"%s\"\n"
          "}\n",
          dirent->inode,
          dirent->rec_len,
          dirent->name_len,
          dirent->file_type,
          dirent->name);
}
