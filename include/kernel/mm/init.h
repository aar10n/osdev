//
// Created by Aaron Gill-Braun on 2022-06-08.
//

#ifndef KERNEL_MM_INIT_H
#define KERNEL_MM_INIT_H

#include <base.h>
#include <queue.h>

extern uintptr_t kernel_address;
extern uintptr_t kernel_virtual_offset;
extern uintptr_t kernel_code_start;
extern uintptr_t kernel_code_end;
extern uintptr_t kernel_data_end;

extern uintptr_t kernel_reserved_start;
extern uintptr_t kernel_reserved_end;
extern uintptr_t kernel_reserved_ptr;

void mm_early_init();
uintptr_t mm_early_alloc_pages(size_t count);

#endif
