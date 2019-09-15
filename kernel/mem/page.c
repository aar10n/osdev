//
// Created by Aaron Gill-Braun on 2019-06-06.
//

#include <stdio.h>

#include <kernel/mem/mm.h>
#include <kernel/mem/page.h>

#define PE_PRESENT 0x01
#define PE_READ_WRITE 0x02
#define PE_USER_SUPERVISOR 0x04
#define PE_WRITE_THROUGH 0x08
#define PE_CACHE_DISABLED 0x16
#define PE_PAGE_SIZE_4MB 0x64

extern uint32_t _page_directory;
static uint32_t *page_directory;

extern uint32_t _kernel_page_table;
static uint32_t *kernel_page_table;


uint32_t *get_page_table(int table_index) {
  uint32_t virtual = 0xFFC00000;
  virtual |= (table_index << 12);
  return (uint32_t *) virtual;
}

//
//
//

void init_paging() {
  page_directory = (uint32_t *) &_page_directory;
  kernel_page_table = (uint32_t *) &_kernel_page_table;

  // Recursive page mappings
  page_directory[1023] = 0 | vtop(page_directory) | PE_READ_WRITE | PE_PRESENT;

  // Kernel page table
  int kernel_page = KERNEL_BASE >> 22;
  page_directory[kernel_page] = 0 | vtop(kernel_page_table) | PE_READ_WRITE | PE_PRESENT;

  uint32_t *kernel_pt = get_page_table(kernel_page);
  for (int i = 0; i < 1024; i++) {
    uintptr_t addr = i * 0x1000;
    kernel_pt[i] = 0 | addr | PE_READ_WRITE | PE_PRESENT;
  }
  page_directory[0] = 0;
}

void map_page(page_t *page) {
  kprintf("page = { \n"
          "  frame = %p\n"
          "  addr = %p\n"
          "  size = %u\n"
          "  flags = %b\n"
          "}\n",
          page->frame,
          page->addr,
          page->size,
          page->flags);

  // get_page_table(page->addr >> 22);
}

void remap_page(page_t *page) {}

void unmap_page(page_t *page) {}
