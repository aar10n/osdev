//
// Created by Aaron Gill-Braun on 2020-11-10.
//

#ifndef KERNEL_SYSCALL_H
#define KERNEL_SYSCALL_H

#include <kernel/base.h>

#include <bits/syscall.h>

#define DEFINE_SYSCALL(name, ret_type, ...) \
  ret_type sys_ ##name(MACRO_JOIN(__VA_ARGS__))

extern void syscall_handler();

#endif
