//
// Created by Aaron Gill-Braun on 2020-09-05.
//

#define __PANIC__
#ifndef KERNEL_PANIC_H
#define KERNEL_PANIC_H

#include <kernel/base.h>

#define PANIC(msg, args...) \
  panic("kernel panic: " msg ", file %s, line %d", ##args, __FILE__, __LINE__);

#define kassert(expression) \
  if (!(expression)) \
    panic("assertion failed: %s, file %s, line %d", #expression, __FILE__, __LINE__);

#define kassertf(expression, msg, ...) \
  if (!(expression)) \
    panic("assertion failed: " msg ", file %s, line %d", ##__VA_ARGS__ __VA_OPT__(,) __FILE__, __LINE__);

#define unreachable panic("unreachable: file %s, line %d", __FILE__, __LINE__)
#define unimplemented(msg) panic("not implemented: " msg ", file %s, line %d", __FILE__, __LINE__)

void panic_early_init();

noreturn void panic(const char *fmt, ...);

#endif
