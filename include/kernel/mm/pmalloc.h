//
// Created by Aaron Gill-Braun on 2022-06-17.
//

#ifndef KERNEL_MM_PMALLOC_H
#define KERNEL_MM_PMALLOC_H

#include <base.h>
#include <mm_types.h>

void init_mem_zones();

page_t *_alloc_pages_zone(mem_zone_type_t zone_type, size_t count, uint32_t flags);
page_t *_alloc_pages(size_t count, uint32_t flags);
page_t *_alloc_pages_at(uintptr_t address, size_t count, uint32_t flags);
void _free_pages(page_t *page);

#endif
