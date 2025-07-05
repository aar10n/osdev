//
// Created by Aaron Gill-Braun on 2024-01-21.
//

#ifndef KERNEL_EXEC_H
#define KERNEL_EXEC_H

#include <kernel/base.h>
#include <kernel/mm_types.h>

#define ENV_MAX 128

#define LIBC_BASE_ADDR 0x7FC0000000

struct pcreds;
struct pstrings;

enum exec_type {
  EXEC_BIN,
  EXEC_DYN,
};

struct exec_image {
  enum exec_type type;        // type of image
  str_t path;                 // path of image
  uintptr_t base;             // virtual base address of image
  uintptr_t entry;            // virtual entry point of image
  uintptr_t phdr;             // virtual address of program header table
  size_t phnum;               // number of program header entries
  size_t size;                // size of loaded image
  vm_desc_t *descs;           // image segment vm descriptors
  struct exec_image *interp;  // interpreter image
};

struct exec_stack {
  uintptr_t base;             // virtual base address of stack
  size_t size;                // size of stack
  size_t off;                 // offset from base
  page_t *pages;              // stack pages (ref)
  vm_desc_t *descs;           // stack vm descriptors
};


int exec_load_image(enum exec_type type, uintptr_t base, cstr_t path, __out struct exec_image **imagep);
int exec_image_setup_stack(
  struct exec_image *image,
  uintptr_t stack_base,
  size_t stack_size,
  struct pcreds *creds,
  struct pstrings *args,
  struct pstrings *env,
  __out struct exec_stack **stackp
);

int exec_free_image(struct exec_image **imagep);
int exec_free_stack(struct exec_stack **stackp);

#endif
