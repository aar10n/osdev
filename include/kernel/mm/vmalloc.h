//
// Created by Aaron Gill-Braun on 2022-06-17.
//

#ifndef KERNEL_MM_VMALLOC_H
#define KERNEL_MM_VMALLOC_H

#include <base.h>
#include <queue.h>
#include <spinlock.h>
#include <mm_types.h>

void init_address_space();
address_space_t *new_address_space();
address_space_t *fork_address_space();

void *_vmap_pages(page_t *pages);
void *_vmap_pages_addr(uintptr_t virt_addr, page_t *pages);
void *_vmap_phys(uintptr_t phys_addr, size_t size, uint32_t flags);
void *_vmap_phys_addr(uintptr_t virt_addr, uintptr_t phys_addr, size_t size, uint32_t flags);

void *_vmap_mmio(uintptr_t phys_addr, size_t size, uint32_t flags);

void *_vmap_mmap(uintptr_t phys_addr, size_t size, uint32_t flags);
void *_vmap_mmap_fixed(uintptr_t virt_addr, uintptr_t phys_addr, size_t size, uint32_t flags);
vm_mapping_t *_vmap_reserve(uintptr_t virt_addr, size_t size);

void _vunmap_pages(page_t *pages);
void _vunmap_addr(uintptr_t virt_addr, size_t size);

vm_mapping_t *_vmap_get_mapping(uintptr_t virt_addr);
uintptr_t _vm_virt_to_phys(uintptr_t virt_addr);
page_t *_vm_virt_to_page(uintptr_t virt_addr);

page_t *valloc_page(uint32_t flags);
page_t *valloc_pages(size_t count, uint32_t flags);
page_t *valloc_zero_pages(size_t count, uint32_t flags);
void vfree_pages(page_t *pages);

void _address_space_print_mappings(address_space_t *space);
void _address_space_to_graphiz(address_space_t *space);
void vm_print_debug_address_space();

#endif
