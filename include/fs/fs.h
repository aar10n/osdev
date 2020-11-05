//
// Created by Aaron Gill-Braun on 2020-10-30.
//

#ifndef FS_FS_H
#define FS_FS_H

#include <base.h>
#include <dirent.h>
#include <inode.h>
#include <file.h>
#include <vfs.h>

#define MAX_PATH 1024
#define MAX_SYMLINKS 40

// Filesystem Capabilities
#define FS_CAP_RO 0x001 // read-only

#define is_root(node) ((node)->parent == (node))

typedef struct fs fs_t;

/* Filesystem operations */
typedef struct fs_impl {
  fs_t *(*mount)(dev_t dev, fs_node_t *mount);
  int (*unmount)(fs_t *fs, fs_node_t *mount);

  inode_t *(*locate)(fs_t *fs, ino_t ino);
  inode_t *(*create)(fs_t *fs, mode_t mode);
  int (*remove)(fs_t *fs, inode_t *inode);
  dirent_t *(*link)(fs_t *fs, inode_t *inode, inode_t *parent, char *name);
  int (*unlink)(fs_t *fs, inode_t *inode, dirent_t *dirent);
  int (*update)(fs_t *fs, inode_t *inode);

  ssize_t (*read)(fs_t *fs, inode_t *inode, off_t offset, size_t nbytes, void *buf);
  ssize_t (*write)(fs_t *fs, inode_t *inode, off_t offset, size_t nbytes, void *buf);
  int (*sync)(fs_t *fs);
} fs_impl_t;

/* A filesystem driver */
typedef struct fs_driver {
  id_t id;          // driver id
  const char *name; // filesystem name
  uint32_t cap;     // filesystem capabilities
  fs_impl_t *impl;  // filesystem operations
} fs_driver_t;

/* A mounted filesystem instance */
typedef struct fs {
  dev_t dev;           // device id
  fs_node_t *mount;    // vfs mount node
  fs_node_t *root;     // fs root node

  void *data;          // fs specific data
  fs_impl_t *impl;     // filesystem operations
  fs_driver_t *driver; // corresponding fs driver
} fs_t;

fs_node_t *__create_fs_node();

void fs_init();

int fs_mount(fs_driver_t *driver, const char *device, const char *path);
int fs_unmount(const char *path);

int fs_open(const char *filename, int flags, mode_t mode);
int fs_close(int fd);
ssize_t fs_read(int fd, void *buf, size_t nbytes);
ssize_t fs_write(int fd, void *buf, size_t nbytes);
off_t fs_lseek(int fd, off_t offset, int whence);

int fs_link(const char *path1, const char *path2);
int fs_unlink(const char *path);
int fs_symlink(const char *path1, const char *path2);
int fs_rename(const char *oldfile, const char *newfile);
int fs_chmod(const char *path, mode_t mode);
int fs_chown(const char *path, uid_t owner, gid_t group);

int fs_opendir(const char *filename);
int fs_closedir(int fd);
int fs_mkdir(const char *path, mode_t mode);
int fs_chdir(const char *path);

dirent_t *fs_readdir(int fd);
long fs_telldir(int fd);
void fs_seekdir(int fd, long loc);

#endif
