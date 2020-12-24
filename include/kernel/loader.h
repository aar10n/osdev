//
// Created by Aaron Gill-Braun on 2020-11-13.
//

#ifndef KERNEL_LOADER_H
#define KERNEL_LOADER_H

#include <base.h>

int load_elf(void *buf, void **entry);

#endif
