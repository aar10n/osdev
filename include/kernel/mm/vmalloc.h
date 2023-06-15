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
void init_ap_address_space();
uintptr_t make_ap_page_tables();
address_space_t *fork_address_space();

// virtual memory api
//

vm_mapping_t *vm_alloc(enum vm_type type, uintptr_t hint, size_t size, uint32_t vm_flags, const char *name);
vm_mapping_t *vm_alloc_rsvd(uintptr_t hint, size_t size, uint32_t vm_flags, const char *name);
vm_mapping_t *vm_alloc_phys(uintptr_t phys_addr, uintptr_t hint, size_t size, uint32_t vm_flags, const char *name);
vm_mapping_t *vm_alloc_pages(page_t *pages, uintptr_t hint, size_t size, uint32_t vm_flags, const char *name);
void *vm_alloc_map_phys(uintptr_t phys_addr, uintptr_t hint, size_t size, uint32_t vm_flags, uint32_t pg_flags, const char *name);
void *vm_alloc_map_pages(page_t *pages, uintptr_t hint, size_t size, uint32_t vm_flags, uint32_t pg_flags, const char *name);
void vm_free(vm_mapping_t *vm);

void *vm_map(vm_mapping_t *vm, uint32_t pg_flags);
void vm_unmap(vm_mapping_t *vm);
int vm_resize(vm_mapping_t *vm, size_t new_size, bool allow_move);

int vm_putpage(vm_mapping_t *vm, size_t off, page_t *page);
page_t *vm_getpage(vm_mapping_t *vm, size_t off);

vm_mapping_t *vm_get_mapping(uintptr_t virt_addr);
uintptr_t vm_virt_to_phys(uintptr_t virt_addr);
uintptr_t vm_mapping_to_phys(vm_mapping_t *vm, uintptr_t virt_addr);

#define virt_to_phys(virt_addr) vm_virt_to_phys((uintptr_t)(virt_addr))

// vmalloc api
//
// The vmalloc functions provide a kmalloc-like interface for allocating regions
// of page backed memory. While all vmalloc functions return virtually contiguous
// memory, only the _phys functions guarentee that the backing pages are physically
// contiguous as well. The pointer returned by all functions points to the start
// of the allocated region. The pointer given to vfree() must be the same as the
// one returned by the vmalloc functions.

void *vmalloc(size_t size, uint32_t pg_flags);
void *vmalloc_at(uintptr_t virt_addr, size_t size, uint32_t pg_flags);
void *vmalloc_phys(size_t size, uint32_t pg_flags);
void *vmalloc_at_phys(uintptr_t phys_addr, size_t size, uint32_t pg_flags);
void vfree(void *ptr);

// debug
void vm_print_mappings(address_space_t *space);
void vm_print_space_tree_graphiz(address_space_t *space);
void vm_print_address_space();


#endif
