//
// Created by Aaron Gill-Braun on 2020-11-13.
//

#ifndef KERNEL_LOADER_H
#define KERNEL_LOADER_H

#include <kernel/base.h>
#include <kernel/mm.h>

#define MAX_ARGV  32
#define MAX_ENVP  64
#define LIBC_BASE_ADDR 0x7FC0000000

typedef struct auxv {
  size_t type;
  size_t value;
} auxv_t;

typedef struct program {
  vm_mapping_t *stack;
  uintptr_t entry;
  uintptr_t sp;
} program_t;

int load_executable(const char *path, char *const argp[], char *const envp[], program_t *program);

#endif
