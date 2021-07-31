//
// Created by Aaron Gill-Braun on 2021-07-27.
//

#ifndef INCLUDE_COMMON_SYSCALLS_H
#define INCLUDE_COMMON_SYSCALLS_H

#include <stdint.h>

#define SYS_EXIT  0
#define SYS_OPEN  1
#define SYS_CLOSE 2
#define SYS_READ  3
#define SYS_WRITE 4
#define SYS_LSEEK 5


#define _syscall(call, ...) __syscall(call, ##__VA_ARGS__)


#define __select_syscall(_0,_1,_2,_3,_4,NAME,...) NAME
#define __syscall(...) __select_syscall(__VA_ARGS__, _syscall4, _syscall3, _syscall2, _syscall1, _syscall0)(__VA_ARGS__)

#define _syscall0(call)                                \
  ({                                                   \
    uint64_t _ret;                                     \
    asm volatile(                                      \
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
    asm volatile(                                      \
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
    asm volatile(                                      \
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
    asm volatile(                                      \
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
    register uint64_t r8 asm("r8") = (uint64_t) arg4;  \
    asm volatile(                                      \
      "syscall"                                        \
      : "=a" (_ret)                                    \
      : "a"(call), "b"(arg1), "c"(arg2),               \
        "d"(arg3), "r"(arg4)                           \
      : "memory"                                       \
    );                                                 \
    return (type) _ret;                                \
  })


#endif
