//
// Created by Aaron Gill-Braun on 2024-01-17.
//

#ifndef KERNEL_LOADELF_H
#define KERNEL_LOADELF_H

#include <kernel/base.h>
#include <kernel/exec.h>

bool elf_is_valid_file(void *file_base, size_t len);
int elf_load_image(enum exec_type type, int fd, void *file_base, size_t len, uintptr_t base, __inout struct exec_image *image);

#endif
