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

struct page;
struct address_space;
struct vm_mapping;
struct mem_zone;

struct intvl_tree;
struct rb_tree;
struct file;
struct bitmap;

// page flags
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
  uint64_t address;              // physical address
  uint32_t flags;                // page flags
  union {
    struct {
      uint32_t list_sz;          // size of list
    } head;// if PG_LIST_HEAD == 1
  };
  struct vm_mapping *new_mapping;
  struct mem_zone *zone;         // owning memory zone
  SLIST_ENTRY(struct page) next;
} page_t;

#define PAGE_PHYS_ADDR(page) ((page)->address)

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
  address_space_t *space;   // owning address space
  str_t name;               // name of the mapping

  uint64_t address;         // virtual address
  size_t size;              // mapping size
  size_t virt_size;         // mapping size in the address space

  union {
    uintptr_t vm_phys;
    struct page *vm_pages;
  };

  LIST_ENTRY(struct vm_mapping) list; // list of mappings
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

#define FRAMEBUFFER_VA      0xFFFFBFFF00000000ULL
#define KERNEL_HEAP_VA      0xFFFFFF8000400000ULL
#define KERNEL_RESERVED_VA  0xFFFFFF8000C00000ULL

#define KERNEL_HEAP_SIZE   (SIZE_4MB + SIZE_2MB)
#define KERNEL_STACK_SIZE  SIZE_16KB

#endif
