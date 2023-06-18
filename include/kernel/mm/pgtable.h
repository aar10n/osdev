//
// Created by Aaron Gill-Braun on 2022-06-18.
//

#ifndef KERNEL_MM_PGTABLE_H
#define KERNEL_MM_PGTABLE_H

#include <kernel/base.h>
#include <kernel/mm_types.h>

void early_init_pgtable();
void *early_map_entries(uintptr_t virt_addr, uintptr_t phys_addr, size_t count, uint32_t flags);

void init_recursive_pgtable(uint64_t *table_virt, uintptr_t table_phys);
void pgtable_unmap_user_mappings();
uint64_t *recursive_map_entry(uintptr_t virt_addr, uintptr_t phys_addr, uint32_t flags, page_t **out_pages);
void recursive_unmap_entry(uintptr_t virt_addr, uint32_t flags);

uintptr_t get_current_pgtable();
void set_current_pgtable(uintptr_t table_phys);
size_t pg_flags_to_size(uint32_t flags);

uintptr_t create_new_ap_page_tables(page_t **out_pages);
uintptr_t deepcopy_fork_page_tables(page_t **out_pages);

#endif
