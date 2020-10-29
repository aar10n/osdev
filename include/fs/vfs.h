//
// Created by Aaron Gill-Braun on 2020-10-28.
//

#ifndef FS_FS_H
#define FS_FS_H

#include <base.h>

#define MAX_FILE_NAME 255

#define PATH_SEPARATOR '/'
#define PATH_SEPARATOR_STRING "/"
#define PATH_UP  ".."
#define PATH_DOT "."

#define O_RDONLY     0x0000
#define O_WRONLY     0x0001
#define O_RDWR       0x0002
#define O_APPEND     0x0008
#define O_CREAT      0x0200
#define O_TRUNC      0x0400
#define O_EXCL       0x0800
#define O_NOFOLLOW   0x1000
#define O_PATH       0x2000
#define O_NONBLOCK   0x4000
#define O_DIRECTORY  0x8000

#define FS_FILE        0x01
#define FS_DIRECTORY   0x02
#define FS_CHARDEVICE  0x04
#define FS_BLOCKDEVICE 0x08
#define FS_PIPE        0x10
#define FS_SYMLINK     0x20
#define FS_MOUNTPOINT  0x40

#define _IFMT   0170000 // type of file
#define _IFDIR  0040000 // directory
#define _IFCHR  0020000 // character special
#define _IFBLK  0060000 // block special
#define _IFREG  0100000 // regular
#define _IFLNK  0120000 // symbolic link
#define _IFSOCK 0140000 // socket
#define _IFIFO  0010000 // fifo

#define ST_RDONLY 0x0
#define ST_NOSUID 0x1

typedef struct fs_node fs_node_t;
typedef struct fs_dirent fs_dirent_t;

/* Filesystem implementation defined file operations */
typedef struct fs_impl {
  ssize_t (*fs_read)(fs_node_t *node, off_t offset, size_t size, void *buf);
  ssize_t (*fs_write)(fs_node_t *node, off_t offset, size_t size, void *buf);
  fs_node_t *(*fs_create)(fs_node_t *node, mode_t mode);
  int (*fs_remove)(fs_node_t *node);
  int (*fs_symlink)(fs_node_t *node, const char *path);
  int (*fs_open)(fs_node_t *node, uint16_t flags);
  int (*fs_close)(fs_node_t *node);
  fs_dirent_t *(*fs_readdir)(fs_node_t *node, uint32_t index);
  fs_node_t *(*fs_finddir)(fs_node_t *node, const char *name);
  ssize_t (*fs_readlink)(fs_node_t *node, char *buf, size_t bufsize);
} fs_impl_t;

typedef struct fs_node {
  dev_t dev;
  ino_t inode;
  mode_t mode;
  nlink_t nlink;
  uid_t uid;
  gid_t gid;
  dev_t rdev;
  off_t size;
  uint32_t mask;

  time_t atime;
  time_t mtime;
  time_t ctime;

  blksize_t blksize;
  blkcnt_t blocks;

  const char *name;
  fs_impl_t *impl;
} fs_node_t;

typedef struct fs_dirent {
  ino_t inode;
  char name[MAX_FILE_NAME];
} fs_dirent_t;


#endif
