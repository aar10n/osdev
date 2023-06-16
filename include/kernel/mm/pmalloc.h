//
// Created by Aaron Gill-Braun on 2022-06-17.
//

#ifndef KERNEL_MM_PMALLOC_H
#define KERNEL_MM_PMALLOC_H

#include <base.h>
#include <mm_types.h>

void init_mem_zones();

// physical memory api
//

page_t *alloc_pages_zone(mem_zone_type_t zone_type, size_t count, uint32_t flags);
page_t *alloc_pages(size_t count, uint32_t flags);
page_t *alloc_pages_at(uintptr_t address, size_t count, uint32_t flags);
void free_pages(page_t *pages);

int reserve_pages(uintptr_t address, size_t count);

bool mm_is_kernel_code_ptr(uintptr_t ptr);
bool mm_is_kernel_data_ptr(uintptr_t ptr);

#endif
