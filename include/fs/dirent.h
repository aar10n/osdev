//
// Created by Aaron Gill-Braun on 2020-10-31.
//

#ifndef FS_DIRENT_H
#define FS_DIRENT_H

#include <base.h>

#define MAX_FILE_NAME 64

typedef struct dirent {
  ino_t inode;
  char name[MAX_FILE_NAME];
} dirent_t;

#endif
