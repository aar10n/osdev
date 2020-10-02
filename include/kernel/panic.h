//
// Created by Aaron Gill-Braun on 2020-09-05.
//

#ifndef KERNEL_PANIC_H
#define KERNEL_PANIC_H

#define PANIC(msg, args...) \
  panic("kernel panic: " msg ", node %s, line %d\n", ##args, __FILE__, __LINE__);

#define kassert(expression) \
  if (!(expression)) \
    panic("assertion failed: " #expression ", node %s, line %d\n", __FILE__, __LINE__);

#define kassertf(expression, msg, args...) \
  if (!(expression)) \
    panic("assertion failed: " msg ", node %s, line %d\n", ##args, __FILE__, __LINE__);

_Noreturn void panic(const char *fmt, ...);

#endif
