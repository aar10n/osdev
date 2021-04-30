//
// Created by Aaron Gill-Braun on 2020-09-30.
//

#ifndef KERNEL_MM_MM_H
#define KERNEL_MM_MM_H

#include <base.h>
#include <boot.h>
#include <bitmap.h>
#include <spinlock.h>

#define get_virt_addr(l4, l3, l2, l1) \
  ((0xFFFFULL << 48) | ((l4) << 39) | ((l3) << 30) | \
   ((l2) << 21) | ((l1) << 12))

#define get_virt_addr_partial(index, level) \
  ((index) << (12 + (((level) - 1) * 9)))

#define page_level_to_shift(level) \
  (12 + (((level) - 1) * 9))

// Zone boundaries
#define Z_LOW_MAX    0x100000    // 1MB
#define Z_DMA_MAX    0x1000000   // 16MB
#define Z_NORMAL_MAX 0x100000000 // 4GB

// Page related definitions
#define PAGE_SIZE 0x1000
#define PAGE_SHIFT 12
#define PAGE_FLAGS_MASK 0xFFF
#define PAGE_FRAME_MASK 0xFFFFFFFFFFFFF000

#define PAGE_SIZE_2MB 0x200000ULL
#define PAGE_SHIFT_2MB 21
#define PAGE_SIZE_1GB 0x40000000ULL
#define PAGE_SHIFT_1GB 30

#define PAGES_TO_SIZE(pages) ((pages) << PAGE_SHIFT)
#define SIZE_TO_PAGES(size) (((size) >> PAGE_SHIFT) + (((size) & PAGE_FLAGS_MASK) ? 1 : 0))

#define PT_INDEX(a) (((a) >> 12) & 0x1FF)
#define PDT_INDEX(a) (((a) >> 21) & 0x1FF)
#define PDPT_INDEX(a) (((a) >> 30) & 0x1FF)
#define PML4_INDEX(a) (((a) >> 39) & 0x1FF)

// page entry flags
#define PE_PRESENT 0x01
#define PE_WRITE 0x02
#define PE_USER 0x04
#define PE_WRITE_THROUGH 0x08
#define PE_CACHE_DISABLE 0x10
#define PE_SIZE 0x80
#define PE_GLOBAL 0x100
// additional mm_alloc_page flags
#define PE_EXEC     0x200
#define PE_2MB_SIZE 0x400
#define PE_1GB_SIZE 0x800
// special flags
#define PE_ASSERT   0x1000
#define PE_FORCE    0x2000

typedef enum {
  ZONE_LOW,
  ZONE_DMA,
  ZONE_NORMAL,
  ZONE_HIGH,
  ZONE_MAX
} zone_type_t;

typedef struct page {
  uint64_t frame;  // the physical address of this page
  uint64_t addr;   // the virtual address of this page
  uint64_t *entry; // when mapped points to the page entry
  union {
    uint16_t raw;
    struct {
      uint16_t present : 1;       // page is present
      uint16_t write : 1;         // read/read-write
      uint16_t user : 1;          // supervisor/user
      uint16_t write_through : 1; // page writeback caching policy
      uint16_t cache_disable : 1; // page caching is disabled
      uint16_t : 2;               // reserved
      uint16_t page_size : 1;     // extended page size
      uint16_t global : 1;        // global page
      uint16_t executable : 1;    // page is executable
      uint16_t page_size_2mb : 1; // page is a 2mb page
      uint16_t page_size_1gb : 1; // page is a 1gb page
      uint16_t zone : 2;          // the zone that contains this page
      uint16_t : 2;               // reserved
    };
  } flags;
  struct page *next;
} page_t;

typedef struct memory_zone {
  zone_type_t type;
  uintptr_t base_addr;
  size_t size;
  bitmap_t *pages;
  spinlock_t lock;
  struct memory_zone *next;
} memory_zone_t;


void mm_init();
page_t *mm_alloc_pages(zone_type_t zone_type, size_t count, uint16_t flags);
page_t *mm_alloc_frame(uintptr_t frame, uint16_t flags);
void mm_free_page(page_t *page);

#define alloc_frame(flags) mm_alloc_pages(ZONE_NORMAL, 1, flags)
#define alloc_frames(count, flags) mm_alloc_pages(ZONE_NORMAL, count, flags)
#define free_frame(page) mm_free_page(page)

#endif
