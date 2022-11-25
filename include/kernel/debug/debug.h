//
// Created by Aaron Gill-Braun on 2022-11-19.
//

#ifndef KERNEL_DEBUG_DEBUG_H
#define KERNEL_DEBUG_DEBUG_H

#include <kernel/base.h>

void debug_early_init();
void debug_init();

char *debug_addr2line(uintptr_t addr);
int debug_unwind(uintptr_t rip, uintptr_t rbp);

#endif
