//
// Created by Aaron Gill-Braun on 2023-05-25.
//

#ifndef KERNEL_FS_H
#define KERNEL_FS_H

#include <vfs_types.h>

void fs_early_init();
int fs_register_type(fs_type_t *fs_type);
fs_type_t *fs_get_type(const char *type);

void fs_init();
__move ventry_t *fs_root_getref();

int fs_mount(const char *path, const char *device, const char *type, int flags);
int fs_unmount(const char *path);

int fs_open(const char *path, int flags, mode_t mode);
int fs_close(int fd);
ssize_t fs_read(int fd, void *buf, size_t len);
ssize_t fs_write(int fd, const void *buf, size_t len);
off_t fs_lseek(int fd, off_t offset, int whence);

int fs_opendir(const char *path);
int fs_closedir(int fd);
ssize_t fs_readdir(int fd, void *dirp, size_t len);
long fs_telldir(int fd);
void fs_seekdir(int fd, long loc);

int fs_dup(int fd);
int fs_dup2(int fd, int newfd);
int fs_fstat(int fd, struct stat *stat);

int fs_stat(const char *path, struct stat *stat);
int fs_create(const char *path, mode_t mode);
int fs_mknod(const char *path, mode_t mode, dev_t dev);
int fs_symlink(const char *target, const char *linkpath);
int fs_link(const char *oldpath, const char *newpath);
int fs_unlink(const char *path);
int fs_mkdir(const char *path, mode_t mode);
int fs_rmdir(const char *path);
int fs_rename(const char *oldpath, const char *newpath);
ssize_t fs_readlink(const char *path, char *buf, size_t bufsiz);


#endif
