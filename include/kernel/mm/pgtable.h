//
// Created by Aaron Gill-Braun on 2022-06-18.
//

#ifndef KERNEL_MM_PGTABLE_H
#define KERNEL_MM_PGTABLE_H

#include <kernel/base.h>
#include <kernel/mm_types.h>

void *early_map_entries(uintptr_t virt_addr, uintptr_t phys_addr, size_t count, uint32_t flags);

void init_recursive_pgtable();
void pgtable_unmap_user_mappings();
uint64_t *recursive_map_entry(uintptr_t virt_addr, uintptr_t phys_addr, uint32_t pg_flags, __move page_t **out_pages);
void recursive_unmap_entry(uintptr_t virt_addr, uint32_t pg_flags);
void recursive_update_entry(uintptr_t virt_addr, uint32_t pg_flags);
void recursive_update_range(uintptr_t virt_addr, size_t size, uint32_t pg_flags);

uintptr_t get_current_pgtable();
void set_current_pgtable(uintptr_t table_phys);
size_t pg_flags_to_size(uint32_t flags);
void zero_unmapped_page(page_t *page);

uintptr_t create_new_ap_page_tables(__move page_t **out_pages);
uintptr_t fork_page_tables(__move page_t **out_pages, bool deepcopy_user);

#endif
