//
// Created by Aaron Gill-Braun on 2020-10-31.
//

#ifndef FS_INODE_H
#define FS_INODE_H

#include <base.h>
#include <lock.h>
#include <rb_tree.h>

// file types backed by inodes
#define I_TYPE_MASK 0xFFF0000
#define I_PERM_MASK 0x000FFFF
#define I_FILE_MASK (S_IFREG | S_IFDIR | S_IFLNK)

#define S_IFMNT  0x0800000 // Filesystem mount.
#define S_IFCHR  0x0400000 // Character special (tty).
#define S_IFIFO  0x0200000 // FIFO special (pipe).
#define S_IFLNK  0x0100000 // Symbolic link.
#define S_IFSOCK 0x0080000 // Socket.
#define S_IFBLK  0x0040000 // Block special.
#define S_IFDIR  0x0020000 // Directory.
#define S_IFREG  0x0010000 // Regular.

#define S_ISUID  0x0004000 // Set-user-ID on execution.
#define S_ISGID  0x0002000 // Set-group-ID on execution.
#define S_IRWXU  0x0000700 // Read, write, execute/search by owner.
#define S_IRUSR  0x0000400 // Read permission, owner.
#define S_IWUSR  0x0000200 // Write permission, owner.
#define S_IXUSR  0x0000100 // Execute/search permission, owner.
#define S_IRWXG  0x0000070 // Read, write, execute/search by group.
#define S_IRGRP  0x0000040 // Read permission, group.
#define S_IWGRP  0x0000020 // Write permission, group.
#define S_IXGRP  0x0000010 // Execute/search permission, group.
#define S_IRWXO  0x0000007 // Read, write, execute/search by others.
#define S_IROTH  0x0000004 // Read permission, others.
#define S_IWOTH  0x0000002 // Write permission, others.
#define S_IXOTH  0x0000001 // Execute/search permission, others.

#define IS_IFMNT(mode) ((mode) & S_IFMNT)
#define IS_IFCHR(mode) ((mode) & S_IFCHR)
#define IS_IFIFO(mode) ((mode) & S_IFIFO)
#define IS_IFLNK(mode) ((mode) & S_IFLNK)
#define IS_IFSOCK(mode) ((mode) & S_IFSOCK)
#define IS_IFBLK(mode) ((mode) & S_IFBLK)
#define IS_IFDIR(mode) ((mode) & S_IFDIR)
#define IS_IFREG(mode) ((mode) & S_IFREG)

typedef struct fs_impl fs_impl_t;
typedef struct fs_node fs_node_t;
typedef struct fs fs_t;

typedef struct inode {
  ino_t ino;
  dev_t dev;
  mode_t mode;
  nlink_t nlink;
  uid_t uid;
  gid_t gid;
  dev_t rdev;
  off_t size;

  time_t atime;
  time_t mtime;
  time_t ctime;

  blksize_t blksize;
  blkcnt_t blocks;

  spinlock_t lock;
  void *data;
} inode_t;

typedef struct inode_table {
  rb_tree_t *inodes;
  spinlock_t lock;
} inode_table_t;

extern inode_table_t *inodes;


inode_table_t *create_inode_table();
inode_t *inode_get(fs_node_t *node);
inode_t *inode_create(fs_t *fs, mode_t mode);
int inode_delete(fs_t *fs, inode_t *inode);


#endif
