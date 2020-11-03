//
// Created by Aaron Gill-Braun on 2020-11-02.
//

#ifndef FS_FILE_H
#define FS_FILE_H

#include <base.h>
#include <lock.h>
#include <rb_tree.h>

#define ACCESS_MODE_MASK 0x1F

// Open file flags
#define O_EXEC      0x000001
#define O_RDONLY    0x000002
#define O_RDWR      0x000004
#define O_SEARCH    0x000008
#define O_WRONLY    0x000010

#define O_APPEND    0x000020
#define O_CLOEXEC   0x000040
#define O_CREAT     0x000080
#define O_DIRECTORY 0x000100
#define O_DSYNC     0x000200
#define O_EXCL      0x000400
#define O_NOCTTY    0x000800
#define O_NOFOLLOW  0x001000
#define O_NONBLOCK  0x002000
#define O_RSYNC     0x004000
#define O_SYNC      0x008000
#define O_TRUNC     0x010000
#define O_TTY_INIT  0x020000

typedef struct fs_node fs_node_t;

typedef struct file {
  int fd;
  int flags;
  off_t offset;
  spinlock_t lock;
  fs_node_t *node;
} file_t;

typedef struct file_table {
  int next_fd;
  rb_tree_t *files;
  spinlock_t lock;
} file_table_t;

file_t *__create_file();
file_table_t *__create_file_table();

#endif
