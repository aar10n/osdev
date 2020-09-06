//
// Created by Aaron Gill-Braun on 2020-09-05.
//

#ifndef KERNEL_PANIC_H
#define KERNEL_PANIC_H

#define PANIC(MSG, ...) panic("kernel panic: " MSG ", file %s, line %d\n", __FILE__, __LINE__);

_Noreturn void panic(const char *fmt, ...);

#endif
