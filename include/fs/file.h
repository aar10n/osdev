//
// Created by Aaron Gill-Braun on 2020-11-02.
//

#ifndef FS_FILE_H
#define FS_FILE_H

#include <base.h>
#include <lock.h>
#include <rb_tree.h>
#include <bitmap.h>

#define ACCESS_MODE_MASK 0x1F
#define MAX_PROC_FILES 1024
#define DIR_FILE_FLAGS (O_DIRECTORY | O_RDONLY)

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

// Seek constants
#define SEEK_SET 1
#define SEEK_CUR 2
#define SEEK_END 3

typedef struct fs_node fs_node_t;

typedef enum file_type {

} file_type_t;

/* A generic file-like object represented by a descriptor. */
typedef struct fs_file {

} fs_file_t;

typedef struct file {
  int fd;
  int flags;
  off_t offset;
  rw_spinlock_t lock;
  fs_node_t *node;
} file_t;

typedef struct file FILE;
typedef struct file DIR;

typedef struct file_table {
  bitmap_t *fds;
  rb_tree_t *files;
  spinlock_t lock;
} file_table_t;

file_table_t *create_file_table();
file_t *file_get(int fd);
file_t *file_create(fs_node_t *node, int flags);
void file_delete(file_t *file);
int file_exists(file_t *file);

#endif
