//
// Created by Aaron Gill-Braun on 2021-01-24.
//

#ifndef FS_UTILS_H
#define FS_UTILS_H

void fs_lsdir(const char *path);
void fs_readfile(const char *path);
void fs_writefile(const char *path, char *string);

#endif
