//
// Created by Aaron Gill-Braun on 2022-06-17.
//

#ifndef KERNEL_MM_VMALLOC_H
#define KERNEL_MM_VMALLOC_H

#include <kernel/base.h>
#include <kernel/queue.h>
#include <kernel/mm_types.h>
#include <kernel/kio.h>

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

void switch_address_space(address_space_t *new_space);

void init_address_space();
void init_ap_address_space();
uintptr_t get_default_ap_pml4();

address_space_t *vm_new_space(uintptr_t min_addr, uintptr_t max_addr, uintptr_t page_table);
address_space_t *vm_fork_space(address_space_t *space, bool deepcopy_user);
address_space_t *vm_new_uspace();

size_t rw_unmapped_pages(__ref page_t *pages, size_t off, kio_t *kio);
void fill_unmapped_pages(__ref page_t *pages, uint8_t v);
static inline void zero_unmapped_pages(__ref page_t *pages) { fill_unmapped_pages(pages, 0); }

// virtual memory api
//
// The following functions make up the core virtual memory api. Userspace mappings
// are created in the current address space, while kernel mappings are shared across
// all address spaces.

int vmap_rsvd(uintptr_t hint, size_t size, uint32_t vm_flags, const char *name);
uintptr_t vmap_phys(uintptr_t phys_addr, uintptr_t hint, size_t size, uint32_t vm_flags, const char *name);
uintptr_t vmap_pages(__move page_t *pages, uintptr_t hint, size_t size, uint32_t vm_flags, const char *name);
uintptr_t vmap_anon(size_t vm_size, uintptr_t hint, size_t size, uint32_t vm_flags, const char *name);

int vm_free(uintptr_t vaddr, size_t size);
int vm_protect(uintptr_t vaddr, size_t len, uint32_t prot);
int vm_resize(uintptr_t vaddr, size_t old_size, size_t new_size, bool allow_move, uintptr_t *new_vaddr);
__move page_t *vm_getpage_cow(uintptr_t vaddr);

uintptr_t vm_virt_to_phys(uintptr_t vaddr);
#define virt_to_phys(virt_addr) vm_virt_to_phys((uintptr_t)(virt_addr))

static always_inline size_t vm_flags_to_size(uint32_t vm_flags) {
  if (vm_flags & VM_HUGE_2MB) {
    return PAGE_SIZE_2MB;
  } else if (vm_flags & VM_HUGE_1GB) {
    return PAGE_SIZE_1GB;
  }
  return PAGE_SIZE;
}

// other space api
//
// The following functions provide an interface for creating user space mappings in
// a specific address space. They do not implicitly modify the current address space.

int other_space_map(address_space_t *uspace, uintptr_t vaddr, uint32_t prot, __move page_t *pages);
int other_space_map_cow(address_space_t *uspace, uintptr_t vaddr, uint32_t prot, __ref page_t *pages);

// vmalloc api
//
// The vmalloc functions provide a kmalloc-like interface for allocating regions
// of mapped memory. The pointer returned by all functions points to the start of
// the mapped region. The pointer given to vfree() must be the same as the one as
// is returned by vmalloc.

void *vmalloc(size_t size, uint32_t vm_flags);
void vfree(void *ptr);

// vm descriptor api
//
// The vm descriptor functions provide an interface to describe virtual mappings
// without actively creating them. These descriptors can be stored and then used
// to create mappings at a later time. Each descriptor may contain a reference to
// pages that should be used to back the mappings, otherwise anonymous mappings
// are created instead. If pages are provided, they must be PG_HEAD pages and
// cover an area of memory equal to the `size` field specified in the descriptor.
// The `vm_size` field is optional but may be used to claim a virtual region
// larger than what is mapped.

vm_desc_t *vm_desc_alloc(uint64_t address, size_t size, uint32_t vm_flags, const char *name, __move page_t *pages);
void vm_desc_free_all(vm_desc_t **descp);
int vm_desc_map(vm_desc_t *descs);
int vm_desc_map_other_space(vm_desc_t *descs, address_space_t *uspace);

// debug
void vm_print_address_space();
void vm_print_mappings(address_space_t *space);
void vm_print_address_space_v2();
void vm_print_format_address_space(address_space_t *space);
void vm_write_format_address_space(int fd, address_space_t *space);
void vm_write_format_address_space_graphiz(int fd, address_space_t *space);

#endif
