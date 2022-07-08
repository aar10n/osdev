#ifndef INCLUDE_ABI_FCNTL_H
#define INCLUDE_ABI_FCNTL_H

#ifdef __KERNEL__
#include <abi/abi.h>
#else
#include <abi-bits/abi.h>
#endif

// constants for fcntl()'s command argument
#define F_DUPFD 1
#define F_DUPFD_CLOEXEC 2
#define F_GETFD 3
#define F_SETFD 4
#define F_GETFL 5
#define F_SETFL 6
#define F_GETLK 7
#define F_SETLK 8
#define F_SETLKW 9
#define F_GETOWN 10
#define F_SETOWN 11

// constants for struct flock's l_type member
#define F_RDLCK 1
#define F_UNLCK 2
#define F_WRLCK 3

// constants for fcntl()'s additional argument of F_GETFD and F_SETFD
#define FD_CLOEXEC 1

// Used by mmap
#define F_SEAL_SHRINK 0x0002
#define F_SEAL_GROW   0x0004
#define F_SEAL_WRITE  0x0008
#define F_GET_SEALS   1034

#define AT_EMPTY_PATH 1
#define AT_SYMLINK_FOLLOW 2
#define AT_SYMLINK_NOFOLLOW 4
#define AT_REMOVEDIR 8
#define AT_EACCESS 512

#define AT_FDCWD -100

// open flags
#define O_EXEC       __MLIBC_O_EXEC
#define O_RDONLY     __MLIBC_O_RDONLY
#define O_RDWR       __MLIBC_O_RDWR
#define O_SEARCH     __MLIBC_O_SEARCH
#define O_WRONLY     __MLIBC_O_WRONLY

#define O_APPEND     __MLIBC_O_APPEND
#define O_CLOEXEC    __MLIBC_O_CLOEXEC
#define O_CREAT      __MLIBC_O_CREAT
#define O_DIRECTORY  __MLIBC_O_DIRECTORY
#define O_DSYNC      __MLIBC_O_DSYNC
#define O_EXCL       __MLIBC_O_EXCL
#define O_NOCTTY     __MLIBC_O_NOCTTY
#define O_NOFOLLOW   __MLIBC_O_NOFOLLOW
#define O_NONBLOCK   __MLIBC_O_NONBLOCK
#define O_RSYNC      __MLIBC_O_RSYNC
#define O_SYNC       __MLIBC_O_SYNC
#define O_TRUNC      __MLIBC_O_TRUNC
#define O_TTY_INIT   -1
#define O_PATH       __MLIBC_O_PATH

#endif
