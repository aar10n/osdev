//
// Created by Aaron Gill-Braun on 2020-10-31.
//

#ifndef FS_DIRENT_H
#define FS_DIRENT_H

#include <base.h>
#include <vfs.h>

#define MAX_FILE_NAME 64

typedef struct dirent {
  ino_t inode;
  char name[MAX_FILE_NAME];
} dirent_t;


dirent_t *dirent_create(fs_node_t *node, const char *name);
int dirent_remove(fs_node_t *node, dirent_t *dirent);

#endif
