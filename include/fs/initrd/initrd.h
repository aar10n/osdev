//
// Created by Aaron Gill-Braun on 2023-04-25.
//

#ifndef FS_INITRD_INITRD_H
#define FS_INITRD_INITRD_H

#include <fs_types.h>

typedef struct initrd_header {
  char signature[6];       // the signature 'I' 'N' 'I' 'T' 'v' '1'
  uint16_t flags;          // initrd flags
  uint32_t total_size;     // total size of the initrd image
  uint32_t data_offset;    // offset from start of image to start of data section
  uint16_t entry_count;    // number of entries in the metadata section
  uint8_t reserved[14];    // reserved
} initrd_header_t;
static_assert(sizeof(struct initrd_header) == 32);

typedef struct initrd_entry {
  uint8_t entry_type;   // type: 'f'=file | 'd'=directory | 'l'=symlink
  uint8_t reserved;     // reserved
  uint16_t path_len;    // length of the file path
  uint32_t data_offset; // offset from start of image to associated data
  uint32_t data_size;   // size of the associated data
  char path[];          // file path
} initrd_entry_t;
static_assert(sizeof(struct initrd_entry) == 12);
// stride = sizeof(struct initrd_entry) + entry.path_len + 1

// super.c
int initrd_sb_mount(struct super_block *sb, struct dentry *mount);
int initrd_sb_unmount(struct super_block *sb);
int initrd_sb_read_inode(struct super_block *sb, struct inode *inode);
// inode.c
int initrd_i_loaddir(inode_t *inode, dentry_t *dentry);

#endif
