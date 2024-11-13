//
// Created by Aaron Gill-Braun on 2021-03-24.
//

#ifndef KERNEL_MM_H
#define KERNEL_MM_H

#include <kernel/base.h>
#include <kernel/mm_types.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/pmalloc.h>
#include <kernel/mm/vmalloc.h>
#include <kernel/mm/init.h>
#include <kernel/mm/file.h>

static inline bool is_kernel_code_ptr(uintptr_t ptr) {
  return ptr >= kernel_code_start && ptr < kernel_code_end;
}

static inline bool is_kernel_data_ptr(uintptr_t ptr) {
  return ptr >= kernel_code_end && ptr < kernel_data_end;
}

static inline bool is_userspace_ptr(uintptr_t ptr) {
  return ptr <= USER_SPACE_END;
}

#endif
