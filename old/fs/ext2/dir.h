//
// Created by Aaron Gill-Braun on 2019-04-22.
//

#ifndef FS_EXT2_DIR_H
#define FS_EXT2_DIR_H

#include <stdint.h>

//
//
// Linked List Directory
//
//

typedef struct __attribute__((packed)) ext2_dirent {
  uint32_t inode;
  uint16_t rec_len;
  uint8_t name_len;
  uint8_t file_type;
  char *name;
} ext2_dirent_t;

// Defined Inode File Types
#define EXT2_FT_UNKNOWN 0  // Unknown File Type
#define EXT2_FT_REG_FILE 1 // Regular File
#define EXT2_FT_DIR 2      // Directory File
#define EXT2_FT_CHRDEV 3   // Character Device
#define EXT2_FT_BLKDEV 4   // Block Device
#define EXT2_FT_FIFO 5     // Buffer File
#define EXT2_FT_SOCK 6     // Socket File
#define EXT2_FT_SYMLINK 7  // Symbolic Link

//
//
// Indexed Directory Format
//
//

// -- Linked Directory Entry: . --
// uint32_t  inode: this directory
// uint16_t  rec_len: 12
// uint8_t   name_len: 1
// uint8_t   file_type: EXT2_FT_DIR=2
// uint8_t   name: .
// uint8_t   padding[3]
// -- Linked Directory Entry: .. --
// uint32_t  inode: parent directory
// uint16_t  rec_len: (blocksize - this entry's length(12))
// uint8_t   name_len: 2
// uint8_t   file_type: EXT2_FT_DIR=2
// uint8_t   name: ..
// uint8_t   padding[2]
// -- Indexed Directory Root Information Structure --
// uint32_t  reserved, zero
// uint8_t   hash version
// uint8_t   info length
// uint8_t   indirect levels
// uint8_t   reserved - unused flags

// Defined Indexed Directory Hash Versions
#define DX_HASH_LEGACY 0
#define DX_HASH_HALF_MD4 1
#define DX_HASH_TEA 2

//
// Indexed Directory Entry
//

// Indexed Directory Entry Structure
typedef struct __attribute__((packed)) {
  uint32_t hash;
  uint32_t block;
} ext2_indexed_dirent_t;

// Indexed Directory Entry Count and Limit Structure
typedef struct __attribute__((packed)) {
  uint16_t limit;
  uint16_t count;
} ext2_indexed_dirent_limits_t;

// Lookup Algorithm
// Lookup is straightforword:
//
//  - Compute a hash of the name
//  - Read the index is_root
//  - Use binary search (linear in the current code) to find the
//    first index or leaf addr that could contain the target hash
//    (in tree order)
//  - Repeat the above until the lowest tree level is reached
//  - Read the leaf directory entry addr and do a normal Ext2
//    directory addr search in it.
//  - If the name is found, return its directory entry and buffer
//  - Otherwise, if the collision bit of the next directory entry is
//    set, continue searching in the successor addr

// Insert Algorithm
// Insertion of new entries into the directory is considerably more complex than lookup, due to the
// need to split leaf blocks when they become full, and to satisfy the conditions that allow hash
// key collisions to be handled reliably and efficiently. I'll just summarize here:
//
//  - Probe the index as for lookup
//  - If the target leaf addr is full, split it and note the addr
//    that will receive the new entry
//  - Insert the new entry in the leaf addr using the normal Ext2
//    directory entry insertion code.


// crc32 hash function


#endif // FS_EXT2_DIR_H
