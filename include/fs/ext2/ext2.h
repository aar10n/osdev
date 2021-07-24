//
// Created by Aaron Gill-Braun on 2021-07-22.
//

#ifndef FS_EXT2_EXT2_H
#define FS_EXT2_EXT2_H

#include <fs.h>

#define EXT2_BLK(blocksz, block) ((block) * ((blocksz) / SEC_SIZE))
#define EXT2_READ(sb, lba, count) EXT2_READX(sb, lba, count, 0)
#define EXT2_READX(sb, lba, count, flags) blkdev_readx((sb)->dev, EXT2_BLK((sb)->blksize, lba), EXT2_BLK((sb)->blksize, count), flags)
#define EXT2_READBUF(sb, lba, count, buf) blkdev_readbuf((sb)->dev, EXT2_BLK((sb)->blksize, lba), EXT2_BLK((sb)->blksize, count), buf)
#define EXT2_WRITE(sb, lba, count, buf) blkdev_write((sb)->dev, EXT2_BLK((sb)->blksize, lba), EXT2_BLK((sb)->blksize, count), buf)


// s_state
#define EXT2_VALID_FS 1
#define EXT2_ERROR_FS 2

// s_magic
#define EXT2_SUPER_MAGIC 0xEF53

// s_errors
#define EXT2_ERRORS_CONTINUE 1
#define EXT2_ERRORS_RO 2
#define EXT2_ERRORS_PANIC 3

// s_creator_os
#define EXT2_OS_LINUX 0
#define EXT2_OS_HURD 1
#define EXT2_OS_MASIX 2
#define EXT2_OS_FREEBSD 3
#define EXT2_OS_LITES 4

// s_rev_level
#define EXT2_GOOD_OLD_REV 0
#define EXT2_DYNAMIC_REV 1

// s_feature_compat
#define EXT2_FEATURE_COMPAT_DIR_PREALLOC 0x0001
#define EXT2_FEATURE_COMPAT_IMAGIC_INODES 0x0002
#define EXT3_FEATURE_COMPAT_HAS_JOURNAL 0x0004
#define EXT2_FEATURE_COMPAT_EXT_ATTR 0x0008
#define EXT2_FEATURE_COMPAT_RESIZE_INO 0x0010
#define EXT2_FEATURE_COMPAT_DIR_INDEX 0x0020

// s_feature_incompat
#define EXT2_FEATURE_INCOMPAT_COMPRESSION 0x0001
#define EXT2_FEATURE_INCOMPAT_FILETYPE 0x0002
#define EXT3_FEATURE_INCOMPAT_RECOVER 0x0004
#define EXT3_FEATURE_INCOMPAT_JOURNAL_DEV 0x0008
#define EXT2_FEATURE_INCOMPAT_META_BG 0x0010

// s_feature_ro_compat
#define EXT2_FEATURE_RO_COMPAT_SPARSE_SUPER 0x0001
#define EXT2_FEATURE_RO_COMPAT_LARGE_FILE 0x0002
#define EXT2_FEATURE_RO_COMPAT_BTREE_DIR 0x0004

// s_algo_bitmap
#define EXT2_LZV1_ALG 0
#define EXT2_LZRW3A_ALG 1
#define EXT2_GZIP_ALG 2
#define EXT2_BZIP2_ALG 3
#define EXT2_LZO_ALG 4

/* superblock */
typedef struct ext2_super {
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
  /* -- EXT2_DYNAMIC_REV Specific -- */
  uint32_t s_first_ino;
  uint16_t s_inode_size;
  uint16_t s_block_group_nr;
  uint32_t s_feature_compat;
  uint32_t s_featrue_incompat;
  uint32_t s_feature_ro_compat;
  uint8_t s_uuid[16];
  char s_volume_name[16];
  char s_last_mounted[64];
  uint32_t s_algo_bitmap;
  /* -- Performance Hints -- */
  uint8_t s_prealloc_blocks;
  uint8_t s_prealloc_dir_blocks;
  uint8_t s_reserved0[2];
  /* -- Journaling Support -- */
  uint8_t s_journal_uuid[16];
  uint32_t s_journal_inum;
  uint32_t s_journal_dev;
  uint32_t s_last_orphan;
  /* -- Directory Indexing Support -- */
  uint32_t s_hash_seed[4];
  uint8_t s_def_hash_version;
  uint8_t s_reserved1[3];
  /* -- Other options -- */
  uint32_t s_default_mount_options;
  uint32_t s_first_meta_bg;
  uint8_t s_reserved2[760];
} ext2_super_t;

/* block group descriptor */
typedef struct ext2_bg_desc {
  uint32_t bg_block_bitmap;
  uint32_t bg_inode_bitmap;
  uint32_t bg_inode_table;
  uint16_t bg_free_blocks_count;
  uint16_t bg_free_inodes_count;
  uint16_t bg_used_dirs_count;
  uint16_t bg_pad;
  uint8_t bg_reserved[12];
} ext2_bg_desc_t;

/*
 * Inodes
 */

// reserved inodes
#define EXT2_BAD_INO 1
#define EXT2_ROOT_INO 2
#define EXT2_ACL_IDX_INO 3
#define EXT2_ACL_DATA_INO 4
#define EXT2_BOOT_LOADER_INO 5
#define EXT2_UNDEL_DIR_INO 6

// i_mode
#define EXT2_TYPE_MASK  0xF000
#define EXT2_S_IFSOCK	0xC000	// socket
#define EXT2_S_IFLNK	0xA000	// symbolic link
#define EXT2_S_IFREG	0x8000	// regular file
#define EXT2_S_IFBLK	0x6000	// block device
#define EXT2_S_IFDIR	0x4000	// directory
#define EXT2_S_IFCHR	0x2000	// character device
#define EXT2_S_IFIFO    0x1000  // fifo

#define EXT2_S_ISUID    0x0800  // Set process User id
#define EXT2_S_ISGID	0x0400	// Set process Group ID
#define EXT2_S_ISVTX    0x0200  // sticky bit

#define EXT2_S_IRUSR	0x0100	// user read
#define EXT2_S_IWUSR	0x0080	// user write
#define EXT2_S_IXUSR	0x0040	// user execute
#define EXT2_S_IRGRP	0x0020	// group read
#define EXT2_S_IWGRP	0x0010	// group write
#define EXT2_S_IXGRP	0x0008	// group execute
#define EXT2_S_IROTH	0x0004	// others read
#define EXT2_S_IWOTH	0x0002	// others write
#define EXT2_S_IXOTH	0x0001	// others execute

// i_flags
#define EXT2_SECRM_FL	      0x00000001  // secure deletion
#define EXT2_UNRM_FL	      0x00000002  // record for undelete
#define EXT2_COMPR_FL	      0x00000004  // compressed file
#define EXT2_SYNC_FL	      0x00000008  // synchronous updates
#define EXT2_IMMUTABLE_FL     0x00000010  // immutable file
#define EXT2_APPEND_FL	      0x00000020  // append only
#define EXT2_NODUMP_FL	      0x00000040  // do not dump/delete file
#define EXT2_NOATIME_FL	      0x00000080  // do not update .i_atime
// -- Reserved for compression usage --
#define EXT2_DIRTY_FL	      0x00000100  // Dirty (modified)
#define EXT2_COMPRBLK_FL      0x00000200  // compressed blocks
#define EXT2_NOCOMPR_FL	      0x00000400  // access raw compressed data
#define EXT2_ECOMPR_FL	      0x00000800  // compression error
// -- End of compression flags --
#define EXT2_BTREE_FL	      0x00001000  // b-tree format directory
#define EXT2_INDEX_FL	      0x00001000  // hash indexed directory
#define EXT2_IMAGIC_FL	      0x00002000  // AFS directory
#define EXT3_JOURNAL_DATA_FL  0x00004000  // journal file data
#define EXT2_RESERVED_FL	  0x80000000  // reserved for ext2 library


/* inode */
typedef struct ext2_inode {
  uint16_t i_mode;
  uint16_t i_uid;
  uint32_t i_size;
  uint32_t i_atime;
  uint32_t i_ctime;
  uint32_t i_mtime;
  uint32_t i_dtime;
  uint16_t i_gid;
  uint16_t i_links_count;
  uint32_t i_blocks;
  uint32_t i_flags;
  uint32_t i_osd1;
  uint32_t i_block[15];
  uint32_t i_generation;
  uint32_t i_file_acl;
  uint32_t i_dir_acl;
  uint32_t i_faddr;
  uint8_t i_osd2[12];
} ext2_inode_t;


/*
 * Linked List Dentry
 */

#define EXT2_FT_UNKNOWN	 0	// Unknown File Type
#define EXT2_FT_REG_FILE 1	// Regular File
#define EXT2_FT_DIR	     2	// Directory File
#define EXT2_FT_CHRDEV	 3	// Character Device
#define EXT2_FT_BLKDEV	 4	// Block Device
#define EXT2_FT_FIFO	 5	// Buffer File
#define EXT2_FT_SOCK	 6	// Socket File
#define EXT2_FT_SYMLINK	 7	// Symbolic Link

typedef struct ext2_ll_dentry {
  uint32_t inode;
  uint16_t rec_len;
  uint8_t name_len;
  uint8_t file_type;
  char name[];
} ext2_ll_dentry_t;

//

typedef struct ext2_data {
  ext2_super_t *sb;
  ext2_bg_desc_t *bgdt;
  uint32_t bg_count;
  // uint8_t *block_bmp;
  // uint8_t *inode_bmp;
  // inode_t *inode_table;
} ext2_data_t;

typedef struct ext2_load_chunk {
  uint32_t start;
  uint32_t len;
  LIST_ENTRY(struct ext2_load_chunk) chunks;
} ext2_load_chunk_t;


void ext2_init();

#endif
