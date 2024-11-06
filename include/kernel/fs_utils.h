//
// Created by Aaron Gill-Braun on 2023-06-08.
//

#ifndef KERNEL_FSUTILS_H
#define KERNEL_FSUTILS_H

#include <kernel/fs.h>

int open(const char *path, mode_t mode);
int dup(int fd);
void close(int fd);
void mount(const char *path, const char *device, const char *type, int flags);
void replace_root(const char *new_root);
void unmount(const char *path);
void touch(const char *path);
void mknod(const char *path, mode_t mode, dev_t dev);
void unlink(const char *path);
void mkdir(const char *path);
void rmdir(const char *path);
void chdir(const char *path);
void stat(const char *path);
void ls(const char *path);
void cat(const char *path);
void echo(const char *path, const char *data);

#endif
