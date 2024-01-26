//
// Created by Aaron Gill-Braun on 2022-06-18.
//

#ifndef KERNEL_MM_PGTABLE_H
#define KERNEL_MM_PGTABLE_H

#include <kernel/base.h>
#include <kernel/mm_types.h>
#include <kernel/kio.h>

void *early_map_entries(uintptr_t virt_addr, uintptr_t phys_addr, size_t count, uint32_t vm_flags);

void init_recursive_pgtable();
void pgtable_unmap_user_mappings();
uint64_t *recursive_map_entry(uintptr_t virt_addr, uintptr_t phys_addr, uint32_t vm_flags, __move page_t **out_pages);
void recursive_unmap_entry(uintptr_t virt_addr, uint32_t vm_flags);
void recursive_update_entry(uintptr_t virt_addr, uint32_t vm_flags);
void recursive_update_range(uintptr_t virt_addr, size_t size, uint32_t vm_flags);

uintptr_t get_current_pgtable();
void set_current_pgtable(uintptr_t table_phys);

static always_inline size_t pg_flags_to_size(uint32_t pg_flags) {
  if (pg_flags & PG_BIGPAGE) {
    return PAGE_SIZE_2MB;
  } else if (pg_flags & PG_HUGEPAGE) {
    return PAGE_SIZE_1GB;
  }
  return PAGE_SIZE;
}

void fill_unmapped_page(page_t *page, uint8_t v);
size_t rw_unmapped_page(page_t *page, size_t off, kio_t *kio);

// creates page mapping entries in an arbitrary pml4. this doesnt use the recursive method
// or leave any implicit mappings in the active page tables.
void nonrecursive_map_pages(uintptr_t pml4, uintptr_t start_vaddr, uint32_t vm_flags, __ref page_t *pages, __move page_t **out_pages);

uintptr_t create_new_ap_page_tables(__move page_t **out_pages);
uintptr_t fork_page_tables(__move page_t **out_pages, bool deepcopy_user);

#endif
