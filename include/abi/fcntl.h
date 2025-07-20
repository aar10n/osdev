//
// Created by Aaron Gill-Braun on 2023-07-07.
//

#ifndef INCLUDE_ABI_FCNTL_H
#define INCLUDE_ABI_FCNTL_H

#include <bits/fcntl.h>

#define loff_t off_t

struct flock {
  short l_type;
  short l_whence;
  off_t l_start;
  off_t l_len;
  pid_t l_pid;
};

#define O_SEARCH   O_PATH
#define O_EXEC     O_PATH
#define O_TTY_INIT 0

#define O_ACCMODE (03|O_SEARCH)
#define O_RDONLY  00
#define O_WRONLY  01
#define O_RDWR    02

#define F_OFD_GETLK 36
#define F_OFD_SETLK 37
#define F_OFD_SETLKW 38

#define F_DUPFD_CLOEXEC 1030

#define F_RDLCK 0
#define F_WRLCK 1
#define F_UNLCK 2

#define FD_CLOEXEC 1

#define AT_FDCWD (-100)
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_REMOVEDIR 0x200
#define AT_SYMLINK_FOLLOW 0x400
#define AT_EACCESS 0x200

#define POSIX_FADV_NORMAL     0
#define POSIX_FADV_RANDOM     1
#define POSIX_FADV_SEQUENTIAL 2
#define POSIX_FADV_WILLNEED   3
#ifndef POSIX_FADV_DONTNEED
#define POSIX_FADV_DONTNEED   4
#define POSIX_FADV_NOREUSE    5
#endif

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#ifndef S_IRUSR
#define S_ISUID 04000
#define S_ISGID 02000
#define S_ISVTX 01000
#define S_IRUSR 0400
#define S_IWUSR 0200
#define S_IXUSR 0100
#define S_IRWXU 0700
#define S_IRGRP 0040
#define S_IWGRP 0020
#define S_IXGRP 0010
#define S_IRWXG 0070
#define S_IROTH 0004
#define S_IWOTH 0002
#define S_IXOTH 0001
#define S_IRWXO 0007
#endif

#endif
