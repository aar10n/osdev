//
// Created by Aaron Gill-Braun on 2023-06-08.
//

#ifndef KERNEL_FSUTILS_H
#define KERNEL_FSUTILS_H

#include <fs.h>

void touch(const char *path);
void mknod(const char *path, mode_t mode, dev_t dev);
void mkdir(const char *path);
void stat(const char *path);
void ls(const char *path);
void cat(const char *path);
void echo(const char *path, const char *data);

#endif
