//
// Created by Aaron Gill-Braun on 2023-06-03.
//

#ifndef INCLUDE_ABI_DIRENT_H
#define INCLUDE_ABI_DIRENT_H

#include "types.h"

#define NAME_MAX 256

struct dirent {
  ino_t d_ino;             // inode number
  uint16_t d_reclen;       // length of this record
  uint8_t d_type;          // type of file
  uint8_t d_namlen;        // length of d_name
  char d_name[];           // filename string (null-terminated)
};

#define DIRENT_MIN_SIZE (sizeof(struct dirent)+2)
#define DIRENT_MAX_SIZE (sizeof(struct dirent)+NAME_MAX+1)

#define DT_UNKNOWN       0
#define DT_REG           1
#define DT_DIR           2
#define DT_LNK           3
#define DT_BLK           4
#define DT_CHR           5
#define DT_SOCK          6
#define DT_FIFO          7

#endif
