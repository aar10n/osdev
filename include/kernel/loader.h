//
// Created by Aaron Gill-Braun on 2020-11-13.
//

#ifndef KERNEL_LOADER_H
#define KERNEL_LOADER_H

#include <kernel/base.h>
#include <kernel/mm_types.h>

#define LIBC_BASE_ADDR 0x7FC0000000

// struct exec_image {
//   str_t path;                 // path of image
//   str_t interp_path;          // interpreter path
//
//   uintptr_t base;             // virtual base address of image
//   uintptr_t entry;            // virtual entry point of image
//   size_t size;                // size of loaded image
//
//   page_t *pages;              // pages containing the loaded image (note: pages are unmapped)
//   vm_proc_desc_t *segments;   // protection descriptors for the image
// };
//
// int load_exec_image(const char *path, uintptr_t base, struct exec_image *imagep);

// typedef struct auxv {
//   size_t type;
//   size_t value;
// } auxv_t;

// typedef struct program {
//   uintptr_t base;
//   uintptr_t entry;
//   size_t end;
//
//   vm_mapping_t *stack;
//   uintptr_t sp;
// } program_t;
//
// int load_executable(const char *path, char *const argp[], char *const envp[], program_t *program);

#endif
