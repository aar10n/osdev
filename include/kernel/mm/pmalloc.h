//
// Created by Aaron Gill-Braun on 2022-06-17.
//

#ifndef KERNEL_MM_PMALLOC_H
#define KERNEL_MM_PMALLOC_H

#include <base.h>
#include <mm_types.h>

void init_mem_zones();

/**
 * Allocates one or more pages of physical memory from the specified zone.
 *
 * @param zone_type The zone to allocate from.
 * @param count The number of pages to allocate.
 * @param flags Page flags.
 * @return The list of allocated page_t structures.
 */
page_t *_alloc_pages_zone(mem_zone_type_t zone_type, size_t count, uint32_t flags);

/**
 * Allocates one or more pages of physical memory. The pages are allocated
 * from the zones in the following order: ZONE_TYPE_HIGH, ZONE_TYPE_NORMAL,
 * ZONE_TYPE_DMA.
 *
 * @param count The number of pages to allocate.
 * @param flags Page flags.
 * @return The list of allocated page_t structures.
 */
page_t *_alloc_pages(size_t count, uint32_t flags);

/**
 * Allocates one or more pages of physical memory starting at the specified
 * physical address.
 *
 * @param address
 * @param count
 * @param flags
 * @return
 */
page_t *_alloc_pages_at(uintptr_t address, size_t count, uint32_t flags);

/**
 * Reserves the specified number of pages starting at `address`.
 * @param address The starting address of the pages to reserve.
 * @param count The number of pages to reserve.
 * @return
 */
int _reserve_pages(uintptr_t address, size_t count);

void _free_pages(page_t *pages);

bool mm_is_kernel_code_ptr(uintptr_t ptr);
bool mm_is_kernel_data_ptr(uintptr_t ptr);

#endif
