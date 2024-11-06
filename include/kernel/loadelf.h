//
// Created by Aaron Gill-Braun on 2024-01-17.
//

#ifndef KERNEL_LOADELF_H
#define KERNEL_LOADELF_H

#include <kernel/base.h>
#include <kernel/mm_types.h>

struct exec_image;

bool elf_is_valid_file(void *file_base, size_t len);
int elf_load_image(int fd, void *file_base, size_t len, uint32_t e_type, uintptr_t base, __inout struct exec_image *image);

#endif
