//
// Created by Aaron Gill-Braun on 2022-06-18.
//

#ifndef KERNEL_MM_PGTABLE_H
#define KERNEL_MM_PGTABLE_H

#include <kernel/base.h>
#include <kernel/mm_types.h>
#include <kernel/kio.h>

void *early_map_entries(uintptr_t vaddr, uintptr_t paddr, size_t count, uint32_t vm_flags);

void init_recursive_pgtable();
uintptr_t get_current_pgtable();
void set_current_pgtable(uintptr_t table_phys);

uint64_t *recursive_map_entry(uintptr_t vaddr, uintptr_t paddr, uint32_t vm_flags, __move page_t **out_pages);
void recursive_unmap_entry(uintptr_t vaddr, uint32_t vm_flags);
void recursive_update_entry_flags(uintptr_t vaddr, uint32_t vm_flags);
void recursive_update_entry(uintptr_t vaddr, uintptr_t frame, uint32_t vm_flags);

void fill_unmapped_page(page_t *page, uint8_t v, size_t off, size_t len);
void fill_unmapped_pages(page_t *pages, uint8_t v, size_t off, size_t len);
size_t rw_unmapped_page(page_t *page, size_t off, kio_t *kio);
size_t rw_unmapped_pages(page_t *pages, size_t off, kio_t *kio);

// The nonrecursive_ functions are able to operate on an aribtrary physical pml4.
// It does not use the recursive method or modify any live mappings in the active
// page tables. This method is much slower than the recursive method and should
// only be used to modify non-active page tables.
void nonrecursive_map_frames(uintptr_t pml4, uintptr_t vaddr, uintptr_t paddr, size_t count, uint32_t vm_flags, __move page_t **out_pages);
void nonrecursive_map_pages(uintptr_t pml4, uintptr_t vaddr, page_t *pages, uint32_t vm_flags, __move page_t **out_pages);

uintptr_t create_new_ap_page_tables(__move page_t **out_pages);
uintptr_t fork_page_tables(__move page_t **out_pages, bool fork_user);

void pgtable_print_debug_pml4(uintptr_t pml4_phys, int max_depth, int start_bound, int end_bound, int bound_level);
void pgtable_print_debug_pml4_user(uintptr_t pml4_phys);

#endif
