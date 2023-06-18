//
// Created by Aaron Gill-Braun on 2022-06-17.
//

#ifndef KERNEL_MM_TYPES_H
#define KERNEL_MM_TYPES_H

#include <base.h>
#include <str.h>
#include <queue.h>
#include <spinlock.h>

#define PAGE_SIZE 0x1000
#define PAGE_SIZE_2MB 0x200000ULL
#define PAGE_SIZE_1GB 0x40000000ULL

#define BIGPAGE_SIZE SIZE_2MB
#define HUGEPAGE_SIZE SIZE_1GB

#define PAGE_SHIFT 12
#define PAGE_FLAGS_MASK 0xFFF
#define PAGE_FRAME_MASK 0xFFFFFFFFFFFFF000

#define PAGES_TO_SIZE(pages) ((pages) << PAGE_SHIFT)
#define SIZE_TO_PAGES(size) (((size) >> PAGE_SHIFT) + (((size) & PAGE_FLAGS_MASK) ? 1 : 0))

struct frame_allocator;
struct page;
struct address_space;
struct vm_mapping;
struct vm_file;
struct mem_zone;

struct intvl_tree;

// page flags
#define PG_WRITE      (1 << 2)
#define PG_EXEC       (1 << 3)
#define PG_USER       (1 << 4)
#define PG_NOCACHE    (1 << 5)
#define PG_WRITETHRU  (1 << 6)
#define PG_GLOBAL     (1 << 7)
/* internal use only */
#define PG_BIGPAGE    (1 << 8)
#define PG_HUGEPAGE   (1 << 9)

typedef struct page {
  uint64_t address;             // physical address
  uint32_t flags;               // page flags
  struct vm_mapping *mapping;   // owning mapping (if mapped)
  struct mem_zone *zone;        // owning memory zone
  struct frame_allocator *fa;   // owning frame allocator
  SLIST_ENTRY(struct page) next;
} page_t;

//

typedef struct address_space {
  struct intvl_tree *tree;
  uintptr_t min_addr;
  uintptr_t max_addr;
  spinlock_t lock;

  size_t num_mappings;
  LIST_HEAD(struct vm_mapping) mappings;
  struct intvl_tree *new_tree;

  uintptr_t page_table;
  LIST_HEAD(struct page) table_pages;
} address_space_t;


enum vm_type {
  VM_TYPE_RSVD, // reserved memory
  VM_TYPE_PHYS, // direct physical mapping
  VM_TYPE_PAGE, // mapped pages
  VM_TYPE_FILE, // lazily mapped file
};

// vm flags
#define VM_USER   0x01  // mapping lives in user space
#define VM_FIXED  0x02  // mapping has fixed address (hint used for address)
#define VM_GUARD  0x04  // leave a guard page at the end of the allocation (only for VM_TYPE_PAGE)
#define VM_STACK  0x08  // mapping grows downwards (only for VM_TYPE_PAGE)
#define VM_GROWS  0x10  // mapping can grow or shrink (conflicts with VM_FIXED)

typedef struct vm_mapping {
  enum vm_type type : 8;    // vm type
  uint32_t flags;           // vm flags
  uint32_t pg_flags;        // page flags (when mapped)

  spinlock_t lock;          // mapping lock
  str_t name;               // name of the mapping
  address_space_t *space;   // owning address space

  uint64_t address;         // virtual address
  size_t size;              // mapping size
  size_t virt_size;         // mapping size in the address space

  union {
    uintptr_t vm_phys;
    struct page *vm_pages;
    struct vm_file *vm_file;
  };

  LIST_ENTRY(struct vm_mapping) list;
} vm_mapping_t;

// address space layout

#define USER_SPACE_START    0x0000000000000000ULL
#define USER_SPACE_END      0x00007FFFFFFFFFFFULL
#define KERNEL_SPACE_START  0xFFFF800000000000ULL
#define KERNEL_SPACE_END    0xFFFFFFFFFFFFFFFFULL

#define FRAMEBUFFER_VA      0xFFFFBFFF00000000ULL
#define KERNEL_HEAP_VA      0xFFFFFF8000400000ULL
#define KERNEL_RESERVED_VA  0xFFFFFF8000C00000ULL

#define KERNEL_HEAP_SIZE   (SIZE_4MB + SIZE_2MB)
#define KERNEL_STACK_SIZE  SIZE_16KB

#endif
