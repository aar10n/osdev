//
// Created by Aaron Gill-Braun on 2021-01-24.
//

#ifndef FS_UTILS_H
#define FS_UTILS_H

#include <fs.h>

void fs_lsdir(const char *path);
void fs_readfile(const char *path);
void fs_writefile(const char *path, char *string);
blkdev_t *fs_get_blkdev(const char *path);

#endif
