//
// Created by Aaron Gill-Braun on 2020-11-13.
//

#ifndef KERNEL_LOADER_H
#define KERNEL_LOADER_H

#include <base.h>
#include <abi/auxv.h>
#include <mm.h>

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

  const char *interp;
  page_t *file_pages;
  page_t *prog_pages;
  struct elf_program *linker;
} elf_program_t;

int load_elf(void *buf, elf_program_t *prog);
int load_elf_file(const char *path, elf_program_t *prog);

#endif
