#ifndef INCLUDE_ABI_STAT_H
#define INCLUDE_ABI_STAT_H

#ifdef __KERNEL__
struct timespec {
  time_t tv_sec;
  long tv_nsec;
};
#else
#include <bits/ansi/time_t.h>
#include <bits/ansi/timespec.h>
#endif

#define S_IFFBF  0x1000000 // Framebuffer special.
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

#define S_IREAD  S_IRUSR
#define S_IWRITE S_IWUSR
#define S_IEXEC  S_IXUSR

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __KERNEL__

typedef struct stat {
  dev_t st_dev;
  ino_t st_ino;
  mode_t st_mode;
  nlink_t st_nlink;
  uid_t st_uid;
  gid_t st_gid;
  dev_t st_rdev;
  off_t st_size;
  struct timespec st_atim;
  struct timespec st_mtim;
  struct timespec st_ctim;
  blksize_t st_blksize;
  blkcnt_t st_blocks;
} stat_t;
#else
struct stat {
  dev_t st_dev;
  ino_t st_ino;
  mode_t st_mode;
  nlink_t st_nlink;
  uid_t st_uid;
  gid_t st_gid;
  dev_t st_rdev;
  off_t st_size;
  struct timespec st_atim;
  struct timespec st_mtim;
  struct timespec st_ctim;
  blksize_t st_blksize;
  blkcnt_t st_blocks;
};
#endif

#ifdef __cplusplus
}
#endif

#endif
