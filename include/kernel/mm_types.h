//
// Created by Aaron Gill-Braun on 2022-06-17.
//

#ifndef KERNEL_MM_TYPES_H
#define KERNEL_MM_TYPES_H

#include <kernel/base.h>
#include <kernel/queue.h>
#include <kernel/str.h>
#include <kernel/ref.h>
#include <kernel/mutex.h>

#ifndef PAGE_SIZE
#define PAGE_SIZE 0x1000
#endif
#define PAGE_SIZE_2MB 0x200000ULL
#define PAGE_SIZE_1GB 0x40000000ULL

#define BIGPAGE_SIZE SIZE_2MB
#define HUGEPAGE_SIZE SIZE_1GB

#define PAGE_SHIFT 12

#define PAGES_TO_SIZE(pages) ((pages) << PAGE_SHIFT)
#define SIZE_TO_PAGES(size) (((size) >> PAGE_SHIFT) + (((size) & 0xFFF) ? 1 : 0))

struct frame_allocator;
struct page;
struct address_space;
struct vm_mapping;
struct vm_anon;

struct intvl_tree;

/*
 * A page of physical memory.
 *
 * The page struct represents a frame of physical memory. Pages can represent
 * different sizes of memory corresponding to the different page sizes supported
 * by the system. The first page in a list of one or more is known as the 'head',
 * and all pages in a list must be of the same size. In general you should only
 * be holding a reference to the head page of a list, and may only modify the list
 * using the `page_list_join` and `page_list_split` functions.
 */
typedef struct page {
  uint64_t address;             // physical frame
  uint32_t flags;               // page flags
  struct {                      // *** valid if PG_HEAD ***
    uint64_t count : 63;        //   number of pages in the list
    uint64_t contiguous : 1;    //   whether the list is physically contiguous
  } head;
  union {
    struct frame_allocator *fa; // owning frame allocator (if PG_OWNING)
    struct page *source;        // source page (if PG_COW)
  };
  struct vm_mapping *mapping;   // owning virtual mapping
  struct page *next;            // next page (ref)
  _refcount;
} page_t;

// page flags
#define PG_BIGPAGE    (1 << 0)
#define PG_HUGEPAGE   (1 << 1)
#define PG_OWNING     (1 << 2)
#define PG_HEAD       (1 << 3)
#define PG_COW        (1 << 4)

#define PG_SIZE_MASK  (PG_BIGPAGE | PG_HUGEPAGE)

/*
 * A virtual address space.
 *
 * The address_space struct represents a section of virtual address space
 * and the mappings contained within it. There is one shared address space for
 * the kernel covering KERNEL_SPACE_START to KERNEL_SPACE_END and each process
 * has its own individual address space covering userspace.
 */
typedef struct address_space {
  uintptr_t min_addr;
  uintptr_t max_addr;
  mtx_t lock;

  size_t num_mappings;
  LIST_HEAD(struct vm_mapping) mappings;
  struct intvl_tree *new_tree;

  uintptr_t page_table;
  LIST_HEAD(struct page) table_pages;
} address_space_t;

#define space_lock(space) __type_checked(struct address_space *, space, mtx_lock(&(space)->lock))
#define space_unlock(space) __type_checked(struct address_space *, space, mtx_unlock(&(space)->lock))
#define space_lock_assert(space, w) __type_checked(struct address_space *, space, mtx_assert(&(space)->lock, w))

enum vm_type {
  VM_TYPE_RSVD, // reserved memory
  VM_TYPE_PHYS, // direct physical mapping
  VM_TYPE_PAGE, // mapped pages
  VM_TYPE_ANON, // on-demand page mapping
  VM_MAX_TYPE,
};

/**
 * A virtual memory mapping.
 *
 * The vm_mapping struct represents a mapping in a virtual address space. There are
 * different kinds of mappings but generally mappings are backed by some memory. Each
 * vm mapping tracks two sizes: a mapped size and a virtual size. The mapped size is
 * the size of the region that is in use whereas the virtual size can be though of as
 * contiguous virtual space reserved for the mapping to grow into. A mapping represents
 * a region of memory with homogenous protection flags. If a sub-range of address space
 * has its protection updated, it results in the splitting of the mapping into two or
 * more adjacent mappings connected by the `sibling` field.
 */
typedef struct vm_mapping {
  enum vm_type type;        // vm type
  uint32_t flags;           // vm flags
  str_t name;               // name of the mapping

  uint64_t address;         // virtual address (start of the mapped region)
  size_t size;              // mapping size
  size_t virt_size;         // mapping size in the address space

  address_space_t *space;   // owning address space
  union {
    uintptr_t vm_phys;
    struct page *vm_pages;
    struct vm_anon *vm_anon;
  };

  LIST_ENTRY(struct vm_mapping) vm_list; // entry in list of vm mappings
} vm_mapping_t;

/////////////
// vm flags
#define VM_READ       (1 << 0)  // mapping is readable
#define VM_WRITE      (1 << 1)  // mapping is writable
#define VM_EXEC       (1 << 2)  // mapping is executable
#define   VM_RDWR       (VM_READ | VM_WRITE)
#define   VM_RDEXC      (VM_READ | VM_EXEC)
#define VM_USER       (1 << 3)  // mapping lives in user space
#define VM_GLOBAL     (1 << 4)  // mapping is global in the TLB
#define VM_NOCACHE    (1 << 5)  // mapping is non-cacheable
#define VM_WRITETHRU  (1 << 6)  // mapping is write-through
#define VM_HUGE_2MB   (1 << 7)  // mapping uses 2M pages
#define VM_HUGE_1GB   (1 << 8)  // mapping uses 1G pages
/* allocation flags */
#define VM_FIXED      (1 << 9)  // mapping has fixed address (hint used for address)
#define VM_STACK      (1 << 10) // mapping grows downwards and has a guard page (only for VM_TYPE_PAGE)
#define VM_MALLOC     (1 << 11) // mapping is a vmalloc allocation
#define VM_NOMAP      (1 << 12) // do not make the mapping active after allocation (cleared after)
#define VM_REPLACE    (1 << 13) // mapping should replace any non-reserved mappings in the range (used with VM_FIXED)
/* internal flags */
#define VM_MAPPED     (1 << 16) // mapping is currently active
#define VM_LINKED     (1 << 18) // mapping was split and is linked to the following mapping
#define VM_SPLIT      (1 << 19) // mapping was split and is the second half of the split
#define VM_COW        (1 << 20) // mapping is a copy-on-write mapping

#define VM_PROT_MASK  0x7    // mask of protection flags
#define VM_FLAGS_MASK 0x3FFF // mask of public flags

/*
 * A description of a future virtual mapping.
 */
typedef struct vm_desc {
  uint64_t address;   // virtual address
  size_t size;        // size of the mapping
  size_t vm_size;     // size of the virtual region containing the mapping
  uint32_t vm_flags;  // vm mapping flags
  const char *name;   // vm name

  struct page *pages; // pages backing the mapping (ref) [anon if NULL]
  struct vm_desc *next;
} vm_desc_t;

// address space layout

#define USER_SPACE_START    0x0000000000000000ULL
#define USER_SPACE_END      0x00007FFFFFFFFFFFULL
#define KERNEL_SPACE_START  0xFFFF800000000000ULL
#define KERNEL_SPACE_END    0xFFFFFFFFFFFFFFFFULL

#define FRAMEBUFFER_VA      0xFFFFBFFF00000000ULL
#define KERNEL_HEAP_VA      0xFFFFFF8000400000ULL
#define KERNEL_RESERVED_VA  0xFFFFFF8000C00000ULL

#define KERNEL_HEAP_SIZE   (6 * SIZE_1MB)
#define KERNEL_STACK_SIZE  SIZE_16KB

#endif
