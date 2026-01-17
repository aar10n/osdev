//
// Created by Aaron Gill-Braun on 2025-01-12.
//

#ifndef KERNEL_FUTEX_H
#define KERNEL_FUTEX_H

#include <kernel/base.h>

void futex_wake_on_exit(int *clear_child_tid);

#endif
