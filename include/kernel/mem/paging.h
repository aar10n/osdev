//
// Created by Aaron Gill-Braun on 2019-06-06.
//

#ifndef KERNEL_MEM_PAGE_H
#define KERNEL_MEM_PAGE_H

#include "mm.h"

#define entry_addr(entry) \
  ((entry) & PE_ADDRESS)
#define entry_flag(entry, flag) \
  ((entry) & flag)
#define pde_to_pt(entry) \
  ((pte_t *) ((entry) & PE_ADDRESS))

#define current_pd ((pde_t *) 0xFFC00000)
#define current_pd_ptr ((pde_t *) (current_pd[1023] & PE_ADDRESS))

// bit masks
#define PE_ADDRESS   0xFFFFF000 // the table or frame address
#define PE_FLAGS     0x00000FFF // the entry flag bits
#define PE_AVAILABLE 0x00000F00 // the os usable bits

// shared flags
#define PE_PRESENT 0x01
#define PE_READ_WRITE 0x02
#define PE_USER 0x04
#define PE_WRITE_THROUGH 0x08
#define PE_CACHE_DISABLED 0x10

// entry specific flags
#define PDE_PAGE_SIZE 0x80
#define PTE_GLOBAL    0x100

// convinience typedefs
typedef uint32_t pde_t;
typedef uint32_t pte_t;

//

pte_t *get_page_table(int index);
pte_t *clone_page_table(const pte_t *src);
pte_t *clone_page_directory(const pte_t *src);

void paging_init();
void map_frame(uintptr_t virt_addr, pte_t pte);
void map_page(page_t *page);
void remap_page(page_t *page);
void unmap_page(page_t *page);

void flush_tlb();
void enable_paging();
void disable_paging();
void switch_page_directory(pde_t *pd);
void copy_page_frame(uintptr_t src, uintptr_t dest);

#endif // KERNEL_MEM_PAGE_H
