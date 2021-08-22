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
#define SYS_FCNTL 8
#define SYS_CREATE 9
#define SYS_MKNOD 10
#define SYS_MKDIR 11
#define SYS_LINK 12
#define SYS_UNLINK 13
#define SYS_SYMLINK 14
#define SYS_RENAME 15
#define SYS_READLINK 16
#define SYS_READDIR 17
#define SYS_TELLDIR 18
#define SYS_SEEKDIR 19
#define SYS_REWINDDIR 20
#define SYS_RMDIR 21
#define SYS_CHDIR 22
#define SYS_CHMOD 23
#define SYS_STAT 24
#define SYS_FSTAT 25
#define SYS_SLEEP 26
#define SYS_NANOSLEEP 27
#define SYS_YIELD 28
#define SYS_GETPID 29
#define SYS_GETPPID 30
#define SYS_GETTID 31
#define SYS_GETUID 32
#define SYS_GETGID 33
#define SYS_GET_CWD 34
#define SYS_MMAP 35
#define SYS_MUNMAP 36
#define SYS_FORK 37
#define SYS_PREAD 38
#define SYS_PWRITE 39
#define SYS_IOCTL 40
#define SYS_SET_FS_BASE 41
#define SYS_PANIC 42
#define SYS_LOG 43
#define SYS_KILL 44
#define SYS_SIGNAL 45
#define SYS_SIGACTION 46

#define _syscall(call, ...) __syscall(call, ##__VA_ARGS__)

#define __select_syscall(_0,_1,_2,_3,_4,_5,_6,NAME,...) NAME
#define __syscall(...) __select_syscall(__VA_ARGS__, _syscall6, _syscall5, _syscall4, _syscall3, _syscall2, _syscall1, _syscall0)(__VA_ARGS__)

#define _syscall0(call)                                   \
  ({                                                      \
    uint64_t _ret;                                        \
    __asm volatile(                                       \
      "syscall"                                           \
      : "=a" (_ret)                                       \
      : "a"(call)                                         \
      : "rcx", "r11", "memory"                            \
    );                                                    \
    _ret;                                                 \
  })

#define _syscall1(call, arg1)                             \
  ({                                                      \
    uint64_t _ret;                                        \
    register uint64_t a __asm("rdi") = (uint64_t) arg1;   \
    __asm volatile(                                       \
      "syscall"                                           \
      : "=a" (_ret)                                       \
      : "a"(call), "r"(a)                                 \
      : "rcx", "r11", "r12", "memory"                     \
    );                                                    \
    _ret;                                                 \
  })

#define _syscall2(call, arg1, arg2)                      \
  ({                                                     \
    uint64_t _ret;                                       \
    register uint64_t a __asm("rdi") = (uint64_t) arg1;  \
    register uint64_t b __asm("rsi") = (uint64_t) arg2;  \
    __asm volatile(                                      \
      "syscall"                                          \
      : "=a" (_ret)                                      \
      : "a"(call), "r"(a), "r"(b)                        \
      : "rcx", "r11", "r12", "memory"                    \
    );                                                   \
    _ret;                                                \
  })

#define _syscall3(call, arg1, arg2, arg3)                \
  ({                                                     \
    uint64_t _ret;                                       \
    register uint64_t a __asm("rdi") = (uint64_t) arg1;  \
    register uint64_t b __asm("rsi") = (uint64_t) arg2;  \
    register uint64_t c __asm("rdx") = (uint64_t) arg3;  \
    __asm volatile(                                      \
      "syscall"                                          \
      : "=a" (_ret)                                      \
      : "a"(call), "r"(a), "r"(b), "r"(c)                \
      : "rcx", "r11", "r12", "memory"                    \
    );                                                   \
    _ret;                                                \
  })

#define _syscall4(call, arg1, arg2, arg3, arg4)          \
  ({                                                     \
    uint64_t _ret;                                       \
    register uint64_t a __asm("rdi") = (uint64_t) arg1;  \
    register uint64_t b __asm("rsi") = (uint64_t) arg2;  \
    register uint64_t c __asm("rdx") = (uint64_t) arg3;  \
    register uint64_t d __asm("r8") = (uint64_t) arg4;   \
    __asm volatile(                                      \
      "syscall"                                          \
      : "=a" (_ret)                                      \
      : "a"(call), "r"(a), "r"(b),                       \
        "r"(c), "r"(d)                                   \
      : "rcx", "r11", "r12", "memory"                    \
    );                                                   \
    _ret;                                                \
  })

#define _syscall5(call, arg1, arg2, arg3, arg4, arg5)    \
  ({                                                     \
    uint64_t _ret;                                       \
    register uint64_t a __asm("rdi") = (uint64_t) arg1;  \
    register uint64_t b __asm("rsi") = (uint64_t) arg2;  \
    register uint64_t c __asm("rdx") = (uint64_t) arg3;  \
    register uint64_t d __asm("r8") = (uint64_t) arg4;   \
    register uint64_t e __asm("r9") = (uint64_t) arg5;   \
    __asm volatile(                                      \
      "syscall"                                          \
      : "=a" (_ret)                                      \
      : "a"(call), "r"(a), "r"(b),                       \
        "r"(c), "r"(d), "r"(e)                           \
      : "rcx", "r11", "r12", "memory"                    \
    );                                                   \
    _ret;                                                \
  })

#define _syscall6(call, arg1, arg2, arg3, arg4, arg5, arg6)  \
  ({                                                     \
    uint64_t _ret;                                       \
    register uint64_t a __asm("rdi") = (uint64_t) arg1;  \
    register uint64_t b __asm("rsi") = (uint64_t) arg2;  \
    register uint64_t c __asm("rdx") = (uint64_t) arg3;  \
    register uint64_t d __asm("r8") = (uint64_t) arg4;   \
    register uint64_t e __asm("r9") = (uint64_t) arg5;   \
    register uint64_t f __asm("r10") = (uint64_t) arg6;  \
    __asm volatile(                                      \
      "syscall"                                          \
      : "=a" (_ret)                                      \
      : "a"(call), "r"(a), "r"(b),                       \
        "r"(c), "r"(d), "r"(e), "r"(f)                   \
      : "rcx", "r11", "r12", "memory"                    \
    );                                                   \
    _ret;                                                \
  })



#endif
