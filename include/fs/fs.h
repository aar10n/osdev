//
// Created by Aaron Gill-Braun on 2020-10-30.
//

#ifndef FS_FS_H
#define FS_FS_H

#include <base.h>
#include <device.h>
#include <blkdev.h>
#include <dirent.h>
#include <file.h>

#define MAX_PATH 1024
#define MAX_SYMLINKS 40

// Filesystem Capabilities
#define FS_CAP_RO 0x001 // read-only

#define is_root(node) ((node)->parent == (node))


// mode flags
#define I_TYPE_MASK 0x0FFF0000
#define I_PERM_MASK 0x0000FFFF
#define I_FILE_MASK (S_IFREG | S_IFDIR | S_IFLNK)

#define S_ISLDD  0x1000000 // Inode is loaded.

#define S_IFMNT  0x0800000 // Filesystem mount.
#define S_IFCHR  0x0400000 // Character special (tty).
#define S_IFIFO  0x0200000 // FIFO special (pipe).
#define S_IFLNK  0x0100000 // Symbolic link.
#define S_IFSOCK 0x0080000 // Socket.
#define S_IFBLK  0x0040000 // Block special.
#define S_IFDIR  0x0020000 // Directory.
#define S_IFREG  0x0010000 // Regular.

#define S_ISUID  0x0004000 // Set-user-ID on execution.
#define S_ISGID  0x0002000 // Set-group-ID on execution.
#define S_IRWXU  0x0000700 // Read, write, execute/search by owner.
#define S_IRUSR  0x0000400 // Read permission, owner.
#define S_IWUSR  0x0000200 // Write permission, owner.
#define S_IXUSR  0x0000100 // Execute/search permission, owner.
#define S_IRWXG  0x0000070 // Read, write, execute/search by group.
#define S_IRGRP  0x0000040 // Read permission, group.
#define S_IWGRP  0x0000020 // Write permission, group.
#define S_IXGRP  0x0000010 // Execute/search permission, group.
#define S_IRWXO  0x0000007 // Read, write, execute/search by others.
#define S_IROTH  0x0000004 // Read permission, others.
#define S_IWOTH  0x0000002 // Write permission, others.
#define S_IXOTH  0x0000001 // Execute/search permission, others.

#define IS_IFMNT(mode) ((mode) & S_IFMNT)
#define IS_IFCHR(mode) ((mode) & S_IFCHR)
#define IS_IFIFO(mode) ((mode) & S_IFIFO)
#define IS_IFLNK(mode) ((mode) & S_IFLNK)
#define IS_IFSOCK(mode) ((mode) & S_IFSOCK)
#define IS_IFBLK(mode) ((mode) & S_IFBLK)
#define IS_IFDIR(mode) ((mode) & S_IFDIR)
#define IS_IFREG(mode) ((mode) & S_IFREG)

#define IS_LOADED(mode) ((mode) & S_ISLDD)

typedef struct fs_impl fs_impl_t;
typedef struct fs_node fs_node_t;
typedef struct fs fs_t;

typedef struct inode {
  ino_t ino;
  dev_t dev;
  mode_t mode;
  nlink_t nlink;
  uid_t uid;
  gid_t gid;
  dev_t rdev;
  off_t size;

  time_t atime;
  time_t mtime;
  time_t ctime;

  blksize_t blksize;
  blkcnt_t blocks;

  mutex_t lock;
  void *data;
} inode_t;




typedef struct fs fs_t;
typedef struct fs_node fs_node_t;

/* Filesystem operations */
typedef struct fs_impl {
  fs_t *(*mount)(blkdev_t *device, fs_node_t *mount);
  int (*unmount)(fs_t *fs, fs_node_t *mount);

  inode_t *(*locate)(fs_t *fs, inode_t *parent, ino_t ino);
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
  blkdev_t *device;    // fs device
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

DIR *fs_opendir(const char *dirname);
int fs_closedir(DIR *dirp);
dirent_t *fs_readdir(DIR *dirp);
void fs_seekdir(DIR *dirp, long loc);
void fs_rewinddir(DIR *dirp);
long fs_telldir(DIR *dirp);

int fs_mkdir(const char *path, mode_t mode);
int fs_chdir(const char *path);

#endif
