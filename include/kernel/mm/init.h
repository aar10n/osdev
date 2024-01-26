//
// Created by Aaron Gill-Braun on 2022-06-08.
//

#ifndef KERNEL_MM_INIT_H
#define KERNEL_MM_INIT_H

#include <kernel/base.h>
#include <kernel/queue.h>

extern uintptr_t kernel_address;
extern uintptr_t kernel_virtual_offset;
extern uintptr_t kernel_code_start;
extern uintptr_t kernel_code_end;
extern uintptr_t kernel_data_end;

extern uintptr_t kernel_reserved_start;
extern uintptr_t kernel_reserved_end;
extern uintptr_t kernel_reserved_ptr;
extern uintptr_t kernel_reserved_va_ptr;

void mm_early_init();
void mm_early_reserve_pages(size_t count);
uintptr_t mm_early_alloc_pages(size_t count);
void *mm_early_map_pages_reserved(uintptr_t phys_addr, size_t count, uint32_t vm_flags);

#endif
