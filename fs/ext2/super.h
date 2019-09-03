//
// Created by Aaron Gill-Braun on 2019-04-22.
//

#ifndef FS_EXT2_SUPERBLOCK_H
#define FS_EXT2_SUPERBLOCK_H

#include <stdint.h>


#define BLOCK_SIZE 1024
#define BLOCK_GROUP_SIZE 8192

//
//
//    Superblock
//
//

typedef struct __attribute__((packed)) {
  uint32_t s_inodes_count;
  uint32_t s_blocks_count;
  uint32_t s_r_blocks_count;
  uint32_t s_free_blocks_count;
  uint32_t s_free_inodes_count;
  uint32_t s_first_data_block;
  uint32_t s_log_block_size;
  uint32_t s_log_frag_size;
  uint32_t s_blocks_per_group;
  uint32_t s_frags_per_group;
  uint32_t s_inodes_per_group;
  uint32_t s_mtime;
  uint32_t s_wtime;
  uint16_t s_mnt_count;
  uint16_t s_max_mnt_count;
  uint16_t s_magic;
  uint16_t s_state;
  uint16_t s_errors;
  uint16_t s_minor_rev_level;
  uint32_t s_lastcheck;
  uint32_t s_checkinterval;
  uint32_t s_creator_os;
  uint32_t s_rev_level;
  uint16_t s_def_resuid;
  uint16_t s_def_resgid;
  // -- EXT2_DYNAMIC_REV Specific --
  uint32_t s_first_ino;
  uint16_t s_inode_size;
  uint16_t s_block_group_nr;
  uint32_t s_feature_compat;
  uint32_t s_feature_incompat;
  uint32_t s_feature_ro_compat;
  uint8_t  s_uuid[16];
  uint8_t  s_volume_name[16];
  uint8_t  s_last_mounted[64];
  uint32_t s_algo_bitmap;
  // -- Performance Hints --
  uint8_t  s_prealloc_blocks;
  uint8_t  s_prealloc_dir_blocks;
  uint16_t s_alignment;
  // -- Journaling Support --
  uint8_t  s_journal_uuid[16];
  uint32_t s_journal_inum;
  uint32_t s_journal_dev;
  uint32_t s_last_orphan;
  // -- Directory Indexing Support --
  uint32_t s_hash_seed[4];
  uint8_t  s_def_hash_version;
  uint8_t  s_padding0;
  uint8_t  s_padding1;
  uint8_t  s_padding2;
  // -- Other options --
  uint32_t s_default_mount_options;
  uint32_t s_first_meta_bg;
  uint8_t  s_unused[760];
} superblock_t;


// s_first_data_block
//
//   s_block_size > 1kB = 0
//   s_block_size = 1kB = 1

// s_log_block_size
//
//   addr size = 1024 << s_log_block_size;

// s_log_frag_size
//
//   if( positive )
//     fragmnet size = 1024 << s_log_frag_size;
//   else
//     framgnet size = 1024 >> -s_log_frag_size;

// s_magic
#define EXT2_SUPER_MAGIC 0xEF53

// s_state
#define EXT2_VALID_FS 1  // Unmounted cleanly
#define EXT2_ERROR_FS 2  // Errors detected

// s_errors
#define EXT2_ERRORS_CONTINUE 1  // continue as if nothing happened
#define EXT2_ERRORS_RO       2  // remount read-only
#define EXT2_ERRORS_PANIC    3  // cause a kernel panic

// s_creator_os
#define EXT2_OS_LINUX   0  // Linux
#define EXT2_OS_HURD    1  // GNU HURD
#define EXT2_OS_MASIX   2  // MASIX
#define EXT2_OS_FREEBSD 3  // FreeBSD
#define EXT2_OS_LITES   4  // Lites

// s_rev_level
#define EXT2_GOOD_OLD_REV 0  // Revision 0
#define EXT2_DYNAMIC_REV  1  // Revision 1 with variable inode sizes, extended attributes, etc.

// s_def_resuid
#define EXT2_DEF_RESUID 0

// s_def_resgid
#define EXT2_DEF_RESGID 0

// s_inode_size
//
// perfect power of 2 and must be smaller or equal
// to the addr size (1 << s_log_block_size).

// s_feature_compat
#define EXT2_FEATURE_COMPAT_DIR_PREALLOC  0x0001  // Block pre-allocation for new directories
#define EXT2_FEATURE_COMPAT_IMAGIC_INODES 0x0002  //
#define EXT3_FEATURE_COMPAT_HAS_JOURNAL   0x0004  // An Ext3 journal exists
#define EXT2_FEATURE_COMPAT_EXT_ATTR      0x0008  // Extended inode attributes are present
#define EXT2_FEATURE_COMPAT_RESIZE_INO    0x0010  // Non-standard inode size used
#define EXT2_FEATURE_COMPAT_DIR_INDEX     0x0020  // Directory indexing (HTree)

// s_feature_incompat
#define EXT2_FEATURE_INCOMPAT_COMPRESSION 0x0001  // Disk/File compression is used
#define EXT2_FEATURE_INCOMPAT_FILETYPE    0x0002  //
#define EXT3_FEATURE_INCOMPAT_RECOVER     0x0004  //
#define EXT3_FEATURE_INCOMPAT_JOURNAL_DEV 0x0008  //
#define EXT2_FEATURE_INCOMPAT_META_BG     0x0010  //

// s_feature_ro_compat
#define EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER 0x0001  // Sparse Superblock
#define EXT2_FEATURE_RO_COMPAT_LARGE_FILE   0x0002  // Large file support, 64-bit file size
#define EXT2_FEATURE_RO_COMPAT_BTREE_DIR    0x0004  // Binary tree sorted directory files

// s_algo_bitmap
#define EXT2_LZV1_ALG   0  // Binary value of 0x00000001
#define EXT2_LZRW3A_ALG 1  // Binary value of 0x00000002
#define EXT2_GZIP_ALG   2  // Binary value of 0x00000004
#define EXT2_BZIP2_ALG  3  // Binary value of 0x00000008
#define EXT2_LZO_ALG    4  // Binary value of 0x00000010


//
//
// Block Group Descriptor Table
//
//

typedef struct __attribute__((packed)) {
  uint32_t bg_block_bitmap;
  uint32_t bg_inode_bitmap;
  uint32_t bg_inode_table;
  uint16_t bg_free_blocks_count;
  uint16_t bg_free_inodes_count;
  uint16_t bg_used_dirs_count;
  uint16_t bg_pad;
  uint8_t  bg_reserved[12];
} bg_descriptor_t;

typedef bg_descriptor_t (bg_descriptor_table_t[32]);


/* Block Bitmap */
// Each bit represents the state of blocks in a group
// 0 = free/available, 1 = used


/* Inode Bitmap */
// Same a `addr bitmap` but each bit represents an inode
// in the `inode table` instead of a addr


#endif //FS_EXT2_SUPERBLOCK_H
