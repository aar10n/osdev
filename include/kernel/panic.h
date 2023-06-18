//
// Created by Aaron Gill-Braun on 2020-09-05.
//

#ifndef KERNEL_PANIC_H
#define KERNEL_PANIC_H

#include <kernel/base.h>

#define PANIC(msg, args...) \
  panic("kernel panic: " msg ", file %s, line %d\n", ##args, __FILE__, __LINE__);

#define kassert(expression) \
  if (!(expression)) \
    panic("assertion failed: %s, file %s, line %d\n", #expression, __FILE__, __LINE__);

#define kassertf(expression, msg, args...) \
  if (!(expression)) \
    panic("assertion failed: " msg ", file %s, line %d\n", ##args, __FILE__, __LINE__);

#define unreachable panic("unreachable: file %s, line %d\n", __FILE__, __LINE__)
#define unimplemented(msg) panic("not implemented: " msg ", file %s, line %d\n", __FILE__, __LINE__)

noreturn void panic(const char *fmt, ...);

#endif
