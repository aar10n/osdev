//
// Created by Aaron Gill-Braun on 2020-11-10.
//

#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

#include <kernel/base.h>

#define SYS_MAX 446

extern void syscall_handler();

#endif
