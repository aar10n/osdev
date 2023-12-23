//
// Created by Aaron Gill-Braun on 2022-06-17.
//

#ifndef KERNEL_MM_VMALLOC_H
#define KERNEL_MM_VMALLOC_H

#include <kernel/base.h>
#include <kernel/queue.h>
#include <kernel/spinlock.h>
#include <kernel/mm_types.h>

typedef __move page_t *(*vm_getpage_t)(struct vm_mapping *vm, size_t off, uint32_t vm_flags, void *data);

typedef struct vm_anon {
  page_t **pages;         // array of pointers to backing pages
  size_t capacity;        // capacity of the pages array
  size_t length;          // length of the pages array

  size_t mapped;          // number of pages mapped
  size_t pg_size;         // size of each page
  vm_getpage_t get_page;  // function to get a page
  void *data;             // get_page data
} vm_anon_t;

void init_address_space();
void init_ap_address_space();
uintptr_t get_default_ap_pml4();

address_space_t *vm_new_space(uintptr_t min_addr, uintptr_t max_addr, uintptr_t page_table);
address_space_t *vm_fork_space(address_space_t *space, bool deepcopy_user);

// virtual memory api
//

vm_mapping_t *vmap(enum vm_type type, uintptr_t hint, size_t size, size_t vm_size, uint32_t vm_flags, const char *name, void *arg);
int vmap_rsvd(uintptr_t hint, size_t size, uint32_t vm_flags, const char *name);
uintptr_t vmap_phys(uintptr_t phys_addr, uintptr_t hint, size_t size, uint32_t vm_flags, const char *name);
uintptr_t vmap_pages(__move page_t *pages, uintptr_t hint, size_t size, uint32_t vm_flags, const char *name);
uintptr_t vmap_anon(size_t vm_size, uintptr_t hint, size_t size, uint32_t vm_flags, const char *name);
void vmap_free(vm_mapping_t *vm);

uintptr_t vm_map_anon(size_t vm_size, uintptr_t hint, size_t size, uint32_t vm_flags, const char *name);
int vm_free(uintptr_t vaddr, size_t len);
int vm_protect(uintptr_t vaddr, size_t len, uint32_t prot);

int vm_resize(vm_mapping_t *vm, size_t new_size, bool allow_move);
__move page_t *vm_getpage(vm_mapping_t *vm, size_t off, bool cowref);
int vm_putpages(vm_mapping_t *vm, __move page_t *pages, size_t off);
uintptr_t vm_mapping_to_phys(vm_mapping_t *vm, uintptr_t virt_addr);
bool vm_mapping_contains(vm_mapping_t *vm, uintptr_t virt_addr);

vm_mapping_t *vm_get_mapping(uintptr_t virt_addr);
uintptr_t vm_virt_to_phys(uintptr_t virt_addr);

#define virt_to_phys(virt_addr) vm_virt_to_phys((uintptr_t)(virt_addr))

static always_inline size_t vm_flags_to_size(uint32_t vm_flags) {
  if (vm_flags & VM_HUGE_2MB) {
    return PAGE_SIZE_2MB;
  } else if (vm_flags & VM_HUGE_1GB) {
    return PAGE_SIZE_1GB;
  }
  return PAGE_SIZE;
}

// vmalloc api
//
// The vmalloc functions provide a kmalloc-like interface for allocating regions
// of page backed memory. While all vmalloc functions return virtually contiguous
// memory, only the _phys functions guarentee that the backing pages are physically
// contiguous as well. The pointer returned by all functions points to the start
// of the allocated region. The pointer given to vfree() must be the same as the
// one returned by the vmalloc functions.

void *vmalloc(size_t size, uint32_t vm_flags);
void *vmalloc_n(size_t size, uint32_t vm_flags, const char *name);
void *vmalloc_at_phys(uintptr_t phys_addr, size_t size, uint32_t vm_flags);
void vfree(void *ptr);

// debug
void vm_print_address_space();
void vm_print_mappings(address_space_t *space);
void vm_print_address_space_v2();
void vm_print_format_address_space(address_space_t *space);
void vm_write_format_address_space(int fd, address_space_t *space);
void vm_write_format_address_space_graphiz(int fd, address_space_t *space);

#endif
