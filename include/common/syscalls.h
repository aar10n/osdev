//
// Created by Aaron Gill-Braun on 2021-07-27.
//

#ifndef INCLUDE_COMMON_SYSCALLS_H
#define INCLUDE_COMMON_SYSCALLS_H

#include <stdint.h>

#define SYS_EXIT 0
#define SYS_EXEC 1
#define SYS_OPEN 2
#define SYS_CLOSE 3
#define SYS_READ 4
#define SYS_WRITE 5
#define SYS_POLL 6
#define SYS_LSEEK 7
#define SYS_CREATE 8
#define SYS_MKNOD 9
#define SYS_MKDIR 10
#define SYS_LINK 11
#define SYS_UNLINK 12
#define SYS_SYMLINK 13
#define SYS_RENAME 14
#define SYS_READLINK 15
#define SYS_READDIR 16
#define SYS_TELLDIR 17
#define SYS_SEEKDIR 18
#define SYS_REWINDDIR 19
#define SYS_RMDIR 20
#define SYS_CHDIR 21
#define SYS_CHMOD 22
#define SYS_STAT 23
#define SYS_FSTAT 24
#define SYS_SLEEP 25
#define SYS_NANOSLEEP 26
#define SYS_YIELD 27
#define SYS_GETPID 28
#define SYS_GETPPID 29
#define SYS_GETTID 30
#define SYS_GETUID 31
#define SYS_GETGID 32
#define SYS_GET_CWD 33
#define SYS_MMAP 34
#define SYS_MUNMAP 35
#define SYS_FORK 36
#define SYS_PREAD 37
#define SYS_PWRITE 38
#define SYS_IOCTL 39


#define _syscall(call, ...) __syscall(call, ##__VA_ARGS__)

#define __select_syscall(_0,_1,_2,_3,_4,_5,_6,NAME,...) NAME
#define __syscall(...) __select_syscall(__VA_ARGS__, _syscall6, _syscall5, _syscall4, _syscall3, _syscall2, _syscall1, _syscall0)(__VA_ARGS__)

#define _syscall0(call)                                \
  ({                                                   \
    uint64_t _ret;                                     \
    __asm volatile(                                      \
      "syscall"                                        \
      : "=a" (_ret)                                    \
      : "a"(call)                                      \
      : "memory"                                       \
    );                                                 \
    _ret;                                              \
  })

#define _syscall1(call, arg1)                          \
  ({                                                   \
    uint64_t _ret;                                     \
    __asm volatile(                                      \
      "syscall"                                        \
      : "=a" (_ret)                                    \
      : "a"(call), "b"(arg1)                           \
      : "memory"                                       \
    );                                                 \
    _ret;                                              \
  })

#define _syscall2(call, arg1, arg2)                    \
  ({                                                   \
    uint64_t _ret;                                     \
    __asm volatile(                                      \
      "syscall"                                        \
      : "=a" (_ret)                                    \
      : "a"(call), "b"(arg1), "c"(arg2)                \
      : "memory"                                       \
    );                                                 \
    _ret;                                              \
  })

#define _syscall3(call, arg1, arg2, arg3)              \
  ({                                                   \
    uint64_t _ret;                                     \
    __asm volatile(                                      \
      "syscall"                                        \
      : "=a" (_ret)                                    \
      : "a"(call), "b"(arg1), "c"(arg2),               \
        "d"(arg3)                                      \
      : "memory"                                       \
    );                                                 \
    _ret;                                              \
  })

#define _syscall4(call, arg1, arg2, arg3, arg4)        \
  ({                                                   \
    uint64_t _ret;                                     \
    register uint64_t r8 __asm("r8") = (uint64_t) arg4;  \
    __asm volatile(                                      \
      "syscall"                                        \
      : "=a" (_ret)                                    \
      : "a"(call), "b"(arg1), "c"(arg2),               \
        "d"(arg3), "r"(arg4)                           \
      : "memory"                                       \
    );                                                 \
    _ret;                                              \
  })

#define _syscall5(call, arg1, arg2, arg3, arg4, arg5)  \
  ({                                                   \
    uint64_t _ret;                                     \
    register uint64_t r8 __asm("r8") = (uint64_t) arg4;  \
    register uint64_t r9 __asm("r9") = (uint64_t) arg5;  \
    __asm volatile(                                      \
      "syscall"                                        \
      : "=a" (_ret)                                    \
      : "a"(call), "b"(arg1), "c"(arg2),               \
        "d"(arg3), "r"(r8), "r"(r9)                    \
      : "memory"                                       \
    );                                                 \
    _ret;                                              \
  })

#define _syscall6(call, arg1, arg2, arg3, arg4, arg5, arg6)  \
  ({                                                   \
    uint64_t _ret;                                     \
    register uint64_t r8 __asm("r8") = (uint64_t) arg4;  \
    register uint64_t r9 __asm("r9") = (uint64_t) arg5;  \
    register uint64_t r10 __asm("r10") = (uint64_t) arg6;\
    __asm volatile(                                      \
      "syscall"                                        \
      : "=a" (_ret)                                    \
      : "a"(call), "b"(arg1), "c"(arg2),               \
        "d"(arg3), "r"(r8), "r"(r9),                   \
        "r"(r10)                                       \
      : "memory"                                       \
    );                                                 \
    _ret;                                              \
  })



#endif
