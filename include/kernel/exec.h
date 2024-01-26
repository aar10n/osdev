//
// Created by Aaron Gill-Braun on 2024-01-21.
//

#ifndef KERNEL_EXEC_H
#define KERNEL_EXEC_H

#include <kernel/base.h>
#include <kernel/mm_types.h>

struct pargs;
struct penv;

struct exec_stack {
  uintptr_t base;             // virtual base address of stack
  size_t off;                 // offset from the pages to the base sp
  page_t *pages;              // stack pages (ref)
  LIST_HEAD(vm_desc_t) descs; // stack vm descriptors
};

struct exec_image {
  str_t path;                 // path of image
  str_t interp_path;          // interpreter path

  uintptr_t base;             // virtual base address of image
  uintptr_t entry;            // virtual entry point of image
  size_t size;                // size of loaded image
  page_t *pages;              // image segment pages
  LIST_HEAD(vm_desc_t) descs; // image segment vm descriptors
};

int exec_load_image(uintptr_t base, const char *path, struct exec_image **imagep);
int exec_setup_stack(uintptr_t base, struct pargs *args, struct penv *env, struct exec_stack **stackp);

int exec_free_image(struct exec_image **imagep);
int exec_free_stack(struct exec_stack **stackp);

#endif
