//
// Created by Aaron Gill-Braun on 2024-01-17.
//

#ifndef KERNEL_LOADELF_H
#define KERNEL_LOADELF_H

#include <kernel/base.h>
#include <kernel/mm_types.h>

struct exec_image;

bool elf_is_valid_file(void *filebuf, size_t len);
int elf_load_image(void *filebuf, size_t len, uint32_t e_type, struct exec_image *image);

#endif
