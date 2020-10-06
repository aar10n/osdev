//
// Created by Aaron Gill-Braun on 2020-09-30.
//

#ifndef KERNEL_MM_MM_H
#define KERNEL_MM_MM_H

#include <base.h>
#include <boot.h>
#include <bitmap.h>

#define KERNEL_OFFSET 0xFFFFFF8000100000
#define STACK_VA 0xFFFFFFA000000000


#define Z_LOW_MAX    0x100000    // 1MB
#define Z_DMA_MAX    0x1000000   // 16MB
#define Z_NORMAL_MAX 0x100000000 // 4GB

#define virt_to_phys(x) (((x) + kernel_phys) - KERNEL_OFFSET)
#define phys_to_virt(x) (KERNEL_OFFSET - (kernel_phys - (x)))

// Page related

#define PAGE_SIZE 0x1000
#define PAGE_SHIFT 12
#define PAGE_MASK 0xFFF

#define PAGE_SIZE_2MB 0x200000
#define PAGE_SHIFT_2MB 21

#define PAGES_TO_SIZE(pages) ((pages) << PAGE_SHIFT)
#define SIZE_TO_PAGES(size) (((size) >> PAGE_SHIFT) + (((size) & PAGE_MASK) ? 1 : 0))

#define PT_INDEX(a) (((a) >> 12) & 0x1FF)
#define PDT_INDEX(a) (((a) >> 21) & 0x1FF)
#define PDPT_INDEX(a) (((a) >> 30) & 0x1FF)
#define PML4_INDEX(a) (((a) >> 39) & 0x1FF)

// Flags

#define PAGE_WRITE 0x1
#define PAGE_USER 0x2
#define PAGE_WRITE_THROUGH 0x4
#define PAGE_CACHE_DISABLE 0x8
// fail if page of requested zone not available
#define ASSERT_ZONE 0x10

//

typedef enum {
  ZONE_LOW,
  ZONE_DMA,
  ZONE_NORMAL,
  ZONE_HIGH,
  ZONE_MAX
} zone_type_t;

typedef struct page {
  uint64_t frame;
  union {
    uint16_t raw;
    struct {
      uint16_t present : 1;
      uint16_t write : 1;
      uint16_t user : 1;
      uint16_t write_through : 1;
      uint16_t cache_disable : 1;
      uint16_t zone : 2;
      uint16_t reserved : 9;
    };
  } flags;
  uint16_t reserved;
  struct page *next;
} page_t;

typedef struct memory_zone {
  zone_type_t type;
  uintptr_t base_addr;
  size_t size;
  bitmap_t *pages;
  struct memory_zone *next;
} memory_zone_t;

void mm_init();
page_t *mm_alloc_page(zone_type_t zone_type, uint16_t flags);
void mm_free_page(page_t *page);

#define alloc_page(flags) mm_alloc_page(ZONE_NORMAL, flags)
#define free_page(page) mm_free_page(page)

#endif
