//
// Created by Aaron Gill-Braun on 2020-11-13.
//

#ifndef KERNEL_LOADER_H
#define KERNEL_LOADER_H

#include <kernel/base.h>
#include <abi/auxv.h>
#include <kernel/mm.h>

typedef struct{
  uint64_t a_type;
  uint64_t a_val;
} auxv_t;

typedef struct elf_program {
  uintptr_t base;
  uint64_t entry;

  uint64_t phdr;
  uint64_t phent;
  uint64_t phnum;

  char *interp;
  struct elf_program *linker;
} elf_program_t;

int elf_load(elf_program_t *prog, void *buf, size_t len);
int elf_load_file(const char *path, elf_program_t *prog);

#endif
