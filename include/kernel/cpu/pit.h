//
// Created by Aaron Gill-Braun on 2019-04-19.
//

#ifndef KERNEL_CPU_PIT_H
#define KERNEL_CPU_PIT_H

#include <stdint.h>

extern volatile uint32_t tick;

void pit_init();

#endif // KERNEL_CPU_PT_H
