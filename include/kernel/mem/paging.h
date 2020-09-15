//
// Created by Aaron Gill-Braun on 2019-06-06.
//

#ifndef KERNEL_MEM_PAGE_H
#define KERNEL_MEM_PAGE_H

#include "mm.h"

#define PE_PRESENT 0x01
#define PE_READ_WRITE 0x02
#define PE_USER_SUPERVISOR 0x04
#define PE_WRITE_THROUGH 0x08
#define PE_CACHE_DISABLED 0x10
#define PE_PAGE_SIZE_4MB 0x80

// page directory entry
typedef union {
  uint32_t raw;
  struct {
    uint32_t present : 1;       // present
    uint32_t read_write : 1;    // read/write
    uint32_t user : 1;          // user/supervisor
    uint32_t write_through : 1; // write-through
    uint32_t cache_disable : 1; // cache disabled
    uint32_t accessed : 1;      // accessed
    uint32_t page_size : 1;     // 4KB/4MB pages
    uint32_t reserved : 1;      // reserved
    uint32_t available : 4;     // available
    uint32_t page_table : 20;   // page table
  };
} pde_t;

// page table entry
typedef union {
  uint32_t raw;
  struct {
    uint32_t present : 1;       // present;
    uint32_t read_write : 1;    // read/write
    uint32_t user : 1;          // user/supervisor
    uint32_t write_through : 1; // write-through
    uint32_t cache_disable : 1; // cache disabled
    uint32_t accessed : 1;      // accessed
    uint32_t dirty : 1;         // dirty
    uint32_t reserved : 1;      // reserved
    uint32_t global : 1;        // global
    uint32_t available : 4;     // available
    uint32_t page_frame : 20;   // page frame
  };
} pte_t;

typedef pde_t page_directory_t[1024];
typedef pte_t page_table_t[1024];

extern page_directory_t *current_directory;

//

page_table_t *get_page_table(int index);
page_table_t *clone_page_table(page_table_t *src);
page_directory_t *clone_page_directory(page_directory_t *src);

void paging_init();
void map_frame(uintptr_t virt_addr, pte_t pte);
void map_page(page_t *page);
void remap_page(page_t *page);
void unmap_page(page_t *page);

void flush_tlb();
void enable_paging();
void disable_paging();
void switch_page_directory(page_directory_t *pd);
void copy_page_frame(uintptr_t src, uintptr_t dest);

#endif // KERNEL_MEM_PAGE_H
