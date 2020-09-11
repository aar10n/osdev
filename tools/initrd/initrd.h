//
// Created by Aaron Gill-Braun on 2020-09-07.
//

#ifndef INITRD_INITRD_H
#define INITRD_INITRD_H

#define INITRD_MAGIC 0xBAE0

// File type
#define FILE_REGULAR   0x1  // A regular file
#define FILE_DIRECTORY 0x2  // A directory file
#define FILE_SYMLINK   0x4  // A symbolic link file

#define DIR_LAST_ENTRY 0x20 // Marks the last entry in a directory

// Limits
#define MAX_FILES     65535     // The maximum number of file nodes
#define MAX_FILE_SIZE 0x8000000 // The maximum size of a single file
#define MAX_NAME_LEN 16         // The maximum name length
#define MAX_SYMLINKS 32         // The maximum number of chained symbolic links

// Ramdisk on-disk structures

typedef struct {
  uint16_t magic;       // initrd magic number
  uint32_t size;        // the total file system size
  uint32_t block_size;  // the size of a "block"
  uint16_t reserved;    // reserved
  uint16_t last_id;     // the last node id
  uint16_t total_nodes; // the total number of file nodes
  uint16_t free_nodes;  // the number of free file nodes
  uint16_t free_offset; // offset to the first free node
  uint32_t file_offset; // offset to the file file (root)
  uint32_t data_offset; // offset to the first data block
} initrd_metadata_t;

typedef struct {
  uint16_t id;     // node id
  uint16_t flags;  // node type + flags
  uint16_t blocks; // the number of blocks used for the data
  uint32_t length; // node data length
  uint32_t offset; // offset from start to node data
  uint32_t dirent; // offset from start to dirent for node
} initrd_file_t;

typedef struct {
  uint16_t node;           // referenced node id
  uint16_t flags;          // dirent flags
  char name[MAX_NAME_LEN]; // entry name
  uint32_t parent;         // offset to parent node
  uint32_t offset;         // offset from start to node
} initrd_dirent_t;

#endif
