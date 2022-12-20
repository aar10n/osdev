//
// Created by Aaron Gill-Braun on 2022-06-17.
//

#ifndef KERNEL_MM_TYPES_H
#define KERNEL_MM_TYPES_H

#include <base.h>
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

struct vm_mapping;
struct mem_zone;
struct intvl_tree;
struct file;
struct bitmap;

// page flags
#define PG_USED       (1 << 0)
#define PG_MAPPED     (1 << 1)
#define PG_READ       0
#define PG_WRITE      (1 << 2)
#define PG_EXEC       (1 << 3)
#define PG_USER       (1 << 4)
#define PG_NOCACHE    (1 << 5)
#define PG_WRITETHRU  (1 << 6)
#define PG_GLOBAL     (1 << 7)
#define PG_BIGPAGE    (1 << 8)
#define PG_HUGEPAGE   (1 << 9)
// flags used during allocation
#define PG_LIST_HEAD  (1 << 16)
#define PG_LIST_TAIL  (1 << 17)
#define PG_FORCE      (1 << 18)
#define PG_ZERO       (1 << 19)

typedef struct page {
  uint64_t address;              // page address
  uint32_t flags;                // page flags (PG_*)
  union {
    struct {
      uint32_t list_sz;          // size of list
    } head;// if PG_LIST_HEAD == 1
    struct {
      uint32_t raw;
    } reserved;
  };
  struct vm_mapping *mapping;    // virtual mapping
  struct mem_zone *zone;         // owning memory zone
  SLIST_ENTRY(struct page) next;
} page_t;

#define PAGE_PHYS_ADDR(page) ((page)->address)
#define PAGE_VIRT_ADDR(page) ((page)->mapping->address)

#define IS_PG_MAPPED(flags)    ((flags) & PG_MAPPED)
#define IS_PG_WRITABLE(flags)  ((flags) & PG_WRITE)
#define IS_PG_USER(flags)      ((flags) & PG_USER)
#define IS_PG_LIST_HEAD(flags) ((flags) & PG_LIST_HEAD)

//

typedef struct address_space {
  struct intvl_tree *root;
  uintptr_t min_addr;
  uintptr_t max_addr;
  spinlock_t lock;

  uintptr_t page_table;
  LIST_HEAD(struct page) table_pages;
} address_space_t;

// vm types
#define VM_TYPE_PHYS   0 // direct physical mapping
#define VM_TYPE_PAGE   1 // mapped page structures
#define VM_TYPE_FILE   2 // mmap'd file
#define VM_TYPE_ANON   3 // mmap'd anonymous memory
#define VM_TYPE_RSVD   4 // reserved memory

// vm attributes
#define VM_ATTR_USER        (1 << 0) // region is in user space
#define VM_ATTR_RESERVED    (1 << 1) // region is reserved
#define VM_ATTR_MMIO        (1 << 2) // region is memory-mapped I/O

typedef struct vm_mapping {
  uint64_t address;    // virtual address
  uint64_t size;       // mapping size

  uint16_t type;       // mapping type (VM_TYPE_*)
  uint16_t attr;       // mapping attributes (VM_ATTR_*)
  uint32_t reserved;   // reserved

  uint32_t flags;      // page flags (PG_*) [if type != VM_TYPE_PAGE]
  spinlock_t lock;     // mapping lock

  const char *name;    // name of the mapping
  union {
    void *ptr;
    uint64_t phys;     // VM_TYPE_PHYS
    struct page *page; // VM_TYPE_PAGE
    struct file *file; // VM_TYPE_FILE
  } data;
} vm_mapping_t;

// zone boundaries
#define ZONE_LOW_MAX    SIZE_1MB
#define ZONE_DMA_MAX    SIZE_16MB
#define ZONE_NORMAL_MAX SIZE_4GB
#define ZONE_HIGH_MAX   UINT64_MAX

typedef enum mem_zone_type {
  ZONE_TYPE_LOW,
  ZONE_TYPE_DMA,
  ZONE_TYPE_NORMAL,
  ZONE_TYPE_HIGH,
  MAX_ZONE_TYPE
} mem_zone_type_t;

typedef struct mem_zone {
  mem_zone_type_t type;
  uintptr_t base;
  size_t size;

  spinlock_t lock;
  struct bitmap *frames;
  LIST_ENTRY(struct mem_zone) list;
} mem_zone_t;

// address space layout

#define USER_SPACE_START    0x0000000000000000ULL
#define USER_SPACE_END      0x00007FFFFFFFFFFFULL
#define KERNEL_SPACE_START  0xFFFF800000000000ULL
#define KERNEL_SPACE_END    0xFFFFFFFFFFFFFFFFULL

#define FRAMEBUFFER_VA      0xFFFFC00000000000ULL
#define MMIO_BASE_VA        0xFFFFC00200000000ULL
#define KERNEL_HEAP_VA      0xFFFFFF8000400000ULL
#define KERNEL_RESERVED_VA  0xFFFFFF8000C00000ULL
#define KERNEL_STACK_TOP_VA 0xFFFFFF8040000000ULL

#define KERNEL_HEAP_SIZE   (SIZE_4MB + SIZE_2MB)
#define KERNEL_STACK_SIZE  SIZE_16KB

#endif
