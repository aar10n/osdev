#ifndef INCLUDE_ABI_ABI_H
#define INCLUDE_ABI_ABI_H

// reserve 3 bits for the access mode
#define __MLIBC_O_ACCMODE 0x1F
#define __MLIBC_O_EXEC   0x000001
#define __MLIBC_O_RDONLY 0x000002
#define __MLIBC_O_RDWR   0x000004
#define __MLIBC_O_SEARCH 0x000008
#define __MLIBC_O_WRONLY 0x000010
// all remaining flags get their own bit
#define __MLIBC_O_APPEND    0x000020
#define __MLIBC_O_CLOEXEC   0x000040
#define __MLIBC_O_CREAT     0x000080
#define __MLIBC_O_DIRECTORY 0x000100
#define __MLIBC_O_DSYNC     0x000200
#define __MLIBC_O_EXCL      0x000400
#define __MLIBC_O_NOCTTY    0x000800
#define __MLIBC_O_NOFOLLOW  0x001000
#define __MLIBC_O_NONBLOCK  0x002000
#define __MLIBC_O_RSYNC     0x004000
#define __MLIBC_O_SYNC      0x008000
#define __MLIBC_O_TRUNC     0x010000
#define __MLIBC_O_PATH      0x040000
#define __MLIBC_O_TMPFILE   0x080000

#endif
