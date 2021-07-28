//
// Created by Aaron Gill-Braun on 2021-07-27.
//

#ifndef INCLUDE_COMMON_SYSCALLS_H
#define INCLUDE_COMMON_SYSCALLS_H

#define _syscall0(type, name)                                \
  type name() {                                              \
    uint64_t _ret;                                           \
    asm volatile(                                            \
      "syscall"                                              \
      : "=a" (_ret)                                          \
      : "a"(__SYS_##name)                                    \
      : "memory"                                             \
    );                                                       \
    return (type) _ret;                                      \
  }

#define _syscall1(type, name, type1, arg1)                   \
  type name(type1 arg1) {                                    \
    uint64_t _ret;                                           \
    asm volatile(                                            \
      "syscall"                                              \
      : "=a" (_ret)                                          \
      : "a"(__SYS_##name), "b"(arg1)                         \
      : "memory"                                             \
    );                                                       \
    return (type) _ret;                                      \
  }

#define _syscall2(type, name, type1, arg1, type2, arg2)      \
  type name(type1 arg1, type2 arg2) {                        \
    uint64_t _ret;                                           \
    asm volatile(                                            \
      "syscall"                                              \
      : "=a" (_ret)                                          \
      : "a"(__SYS_##name), "b"(arg1), "c"(arg2)              \
      : "memory"                                             \
    );                                                       \
    return (type) _ret;                                      \
  }

#define _syscall3(type, name, type1, arg1, type2, arg2, type3, arg3) \
  type name(type1 arg1, type2 arg2, type3 arg3) {            \
    uint64_t _ret;                                           \
    asm volatile(                                            \
      "syscall"                                              \
      : "=a" (_ret)                                          \
      : "a"(__SYS_##name), "b"(arg1), "c"(arg2), "d"(arg3)   \
      : "memory"                                             \
    );                                                       \
    return (type) _ret;                                      \
  }

#define _syscall4(type, name, type1, arg1, type2, arg2, type3, arg3, type4, arg4) \
  type name(type1 arg1, type2 arg2, type3 arg3, type4 arg4) { \
    uint64_t _ret;                                           \
    register uint64_t r8 asm("r8") = (uint64_t) arg4;        \
    asm volatile(                                            \
      "syscall"                                              \
      : "=a" (_ret)                                          \
      : "a"(__SYS_##name), "b"(arg1),                        \
        "c"(arg2), "d"(arg3), "r"(arg4)                      \
      : "memory"                                             \
    );                                                       \
    return (type) _ret;                                      \
  }


/* SYSCALLS */
#define __SYS_exit  0
#define __SYS_open  1
#define __SYS_close 2
#define __SYS_read  3
#define __SYS_write 4
#define __SYS_lseek 5


#endif
