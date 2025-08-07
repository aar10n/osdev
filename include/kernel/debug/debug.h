//
// Created by Aaron Gill-Braun on 2022-11-19.
//

#ifndef KERNEL_DEBUG_DEBUG_H
#define KERNEL_DEBUG_DEBUG_H

#include <kernel/base.h>

typedef struct stackframe {
  struct stackframe *rbp;
  uint64_t rip;
} stackframe_t;

void debug_init();

const char *debug_function_name(uintptr_t addr);

char *debug_addr2line(uintptr_t addr);
int debug_unwind(uintptr_t rip, uintptr_t rbp);
void debug_unwind_any(uintptr_t rip, uintptr_t rbp);

#endif
