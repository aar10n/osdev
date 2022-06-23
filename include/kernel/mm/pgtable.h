//
// Created by Aaron Gill-Braun on 2022-06-18.
//

#ifndef KERNEL_MM_PGTABLE_H
#define KERNEL_MM_PGTABLE_H

#include <base.h>
#include <mm_types.h>

size_t pg_flags_to_size(uint32_t flags);

void *early_map_entries(uintptr_t virt_addr, uintptr_t phys_addr, size_t count, uint32_t flags);

uint64_t *recursive_map_entry(uintptr_t virt_addr, uintptr_t phys_addr, uint32_t flags);
void recursive_unmap_entry(uintptr_t virt_addr, uint32_t flags);

#endif
