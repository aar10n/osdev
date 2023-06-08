//
// Created by Aaron Gill-Braun on 2023-06-03.
//

#ifndef INCLUDE_ABI_DIRENT_H
#define INCLUDE_ABI_DIRENT_H

#include "types.h"

struct dirent {
  ino_t d_ino;              // inode number
  off_t d_off;              // offset to the next dirent
  unsigned short d_reclen;  // length of this record
  unsigned char d_type;     // type of file
  char d_name[];            // filename (null-terminated)
                            //    length = d_reclen - offsetof(struct dirent, d_name) - 1
};

#define DT_UNKNOWN       0
#define DT_REG           1
#define DT_DIR           2
#define DT_LNK           3
#define DT_BLK           4
#define DT_CHR           5
#define DT_SOCK          6
#define DT_FIFO          7

#endif
