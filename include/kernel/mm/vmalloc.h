//
// Created by Aaron Gill-Braun on 2022-06-17.
//

#ifndef KERNEL_MM_VMALLOC_H
#define KERNEL_MM_VMALLOC_H

#include <kernel/base.h>
#include <kernel/queue.h>
#include <kernel/mm_types.h>
#include <kernel/kio.h>

struct vnode;

void switch_address_space(address_space_t *new_space) __used;

void init_address_space();
void init_ap_address_space();
uintptr_t get_default_ap_pml4();

address_space_t *vm_new_space(uintptr_t min_addr, uintptr_t max_addr, uintptr_t page_table);
address_space_t *vm_fork_space(address_space_t *space, bool fork_user);
address_space_t *vm_new_empty_space();
void vm_clear_user_space(address_space_t *space);

//     vmap api
//
// The vmap functions create, modify and free virtual memory mappings.

uintptr_t vmap_rsvd(uintptr_t hint, size_t size, uint32_t vm_flags, const char *name);
uintptr_t vmap_phys(uintptr_t phys_addr, uintptr_t hint, size_t size, uint32_t vm_flags, const char *name);
uintptr_t vmap_pages(__ref page_t *pages, uintptr_t hint, size_t size, uint32_t vm_flags, const char *name);
uintptr_t vmap_file(struct vm_file *file, uintptr_t hint, size_t vm_size, uint32_t vm_flags, const char *name);
uintptr_t vmap_anon(size_t vm_size, uintptr_t hint, size_t size, uint32_t vm_flags, const char *name);
void *vm_mmap(uintptr_t addr, size_t len, int prot, int flags, int fd, off_t off);

int vmap_free(uintptr_t vaddr, size_t size);
int vmap_protect(uintptr_t vaddr, size_t len, uint32_t vm_prot);
int vmap_resize(uintptr_t vaddr, size_t old_size, size_t new_size, bool allow_move, uintptr_t *new_vaddr);

__ref page_t *vm_getpage(uintptr_t vaddr);
__ref page_t *vm_getpage_cow(uintptr_t vaddr);
int vm_validate_user_ptr(uintptr_t vaddr, bool write);
uintptr_t vm_virt_to_phys(uintptr_t vaddr);
#define virt_to_phys(virt_addr) vm_virt_to_phys((uintptr_t)(virt_addr))

//     vmap_other api
//
// The vmap_other functions create virtual memory mappings in a specific address space.
// These functions should not be used to create mappings in the current address space.

uintptr_t vmap_other_rsvd(address_space_t *uspace, uintptr_t vaddr, size_t size, uint32_t vm_flags, const char *name);
uintptr_t vmap_other_phys(address_space_t *uspace, uintptr_t paddr, uintptr_t vaddr, size_t size, uint32_t vm_flags, const char *name);
uintptr_t vmap_other_pages(address_space_t *uspace, __ref page_t *pages, uintptr_t hint, size_t size, uint32_t vm_flags, const char *name);
uintptr_t vmap_other_file(address_space_t *uspace, struct vm_file *file, uintptr_t vaddr, size_t vm_size, uint32_t vm_flags, const char *name);
uintptr_t vmap_other_anon(address_space_t *uspace, size_t vm_size, uintptr_t vaddr, size_t size, uint32_t vm_flags, const char *name);

// defined in pgtable.c
size_t rw_unmapped_pages(page_t *pages, size_t off, kio_t *kio);
void fill_unmapped_pages(page_t *pages, uint8_t v, size_t off, size_t len);

// vm descriptor api
//
// The vm descriptor api provides a way to describe future vm mappings without
// actively creating them. Each descriptor defines the type, size, address and
// flags of the mapping, along with the type associated data. The vm_desc api
// supports operating on both the current address space and other address spaces.

vm_desc_t *vm_desc_alloc(enum vm_type type, uint64_t address, size_t size, uint32_t vm_flags, const char *name, void *data);
int vm_desc_map_space(address_space_t *uspace, vm_desc_t *descs);
void vm_desc_free_all(vm_desc_t **descp);

// vmalloc api
//
// The vmalloc functions provide a kmalloc-like interface for allocating regions
// of mapped memory. The pointer returned by all functions points to the start of
// the mapped region. The pointer given to vfree() must be the same as the one as
// is returned by vmalloc.

void *vmalloc(size_t size, uint32_t vm_flags);
void vfree(void *ptr);

// debug
void vm_print_address_space();
void vm_print_mappings(address_space_t *space);
void vm_print_address_space_v2(bool user, bool kernel);
void vm_print_format_address_space(address_space_t *space);


#endif
