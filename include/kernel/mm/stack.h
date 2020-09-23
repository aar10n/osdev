//
// Created by Aaron Gill-Braun on 2020-09-19.
//

#ifndef KERNEL_MEM_STACK_H
#define KERNEL_MEM_STACK_H

extern uintptr_t stack_top;

void relocate_stack(uintptr_t new_stack_top, uint32_t size);

#endif
