//
// Created by Aaron Gill-Braun on 2019-08-06.
//

#ifndef FS_FS_H
#define FS_FS_H

#include <stdbool.h>
#include <stdint.h>

/* ---- File Modes ---- */

// node type
#define FS_UNKNOWN   0x00 // unknown node type
#define FS_FILE      0x01 // regular node
#define FS_DIRECTORY 0x02 // directory node
#define FS_CHARDEV   0x03 // character device
#define FS_BLOCKDEV  0x04 // block device
#define FS_FIFO      0x05 // buffer node
#define FS_SOCKET    0x06 // socket node
#define FS_SYMLINK   0x07 // symbolic link

#define FS_MOUNT     0x08 // node is mountpoint

/* ---- Access Rights ---- */

/* ---- Structure Types ---- */

struct fs_type;

typedef struct super {
  uint16_t block_size;       // the size of a "block"
  uint16_t inode_size;       // the size of an inode
  uint32_t inode_count;      // total inode count
  uint32_t block_count;      // total block count
  uint32_t rblock_count;     // reserved block count
  uint32_t free_block_count; // free block count
  uint32_t free_inode_count; // free inode count
  uint32_t blocks_per_group; // blocks per group
  uint32_t inodes_per_group; // inodes per group
  uint32_t inodes_per_block; // inodes per block
  uint32_t first_inode;      // index of first usable inode
  char name[16];             // volume name
} super_t;

typedef struct inode {
  uint32_t inode;      // inode number
  uint32_t mode;       // node type and flags
  uint16_t perms;      // node permissions
  uint16_t uid;        // owning user id
  uint16_t gid;        // owning group id
  uint32_t size;       // node size in bytes
  uint16_t links;      // the number of links to the inode
  uint32_t ctime;      // creation time
  uint32_t atime;      // last access time
  uint32_t mtime;      // last modification time
  uint32_t flags;      // implementation defined flags
  struct inode *ptr;   // a pointer to an inode (used by links)
  struct fs_type *fs;  // a pointer to the owning filesystem
} inode_t;

typedef struct dirent {
  uint32_t inode;    // inode number
  uint8_t file_type; // the node type
  uint8_t name_len;  // length of the name
  char *name;        // the node name
} dirent_t;

/* ---- Function Types ---- */

typedef int (*fs_mount_t)(struct fs_type *fs_type);
typedef int (*fs_inode_read_t)(inode_t *root, uint32_t inode, inode_t *result);
typedef int (*fs_inode_write_t)(inode_t *node);
typedef int (*fs_inode_open_t)(inode_t *node);
typedef int (*fs_inode_close_t)(inode_t *node);
typedef int (*fs_readdir_t)(inode_t *node, int index, dirent_t *result);
typedef int (*fs_finddir_t)(inode_t *parent, char *name, dirent_t *result);

typedef struct fs_type {
  uint32_t id;         // the id of the registered filesystem
  const char *name;    // name of the node system
  inode_t *root;       // the root point of this node system
  struct super *super; // the super block structure
  // functions
  fs_mount_t mount;
  fs_inode_read_t read;
  fs_inode_write_t write;
  fs_inode_open_t open;
  fs_inode_close_t close;
  fs_readdir_t readdir;
  fs_finddir_t finddir;
} fs_type_t;

/* ---- Public API ---- */

void fs_register_type(fs_type_t *type);
void fs_unregister_type(fs_type_t *type);

int fs_read(inode_t *root, uint32_t inode, inode_t *result);
int fs_write(inode_t *node);
int fs_open(inode_t *node);
int fs_close(inode_t *node);
int fs_readdir(inode_t *node, int index, dirent_t *result);
int fs_finddir(inode_t *node, char *name, dirent_t *result);

#endif // FS_FS_H
