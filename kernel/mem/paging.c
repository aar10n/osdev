//
// Created by Aaron Gill-Braun on 2019-06-06.
//

#include <stdio.h>

#include <kernel/mem/mm.h>
#include <kernel/mem/paging.h>

extern uint32_t _page_directory;
static uint32_t *page_directory;

extern uint32_t _kernel_page_table;
static uint32_t *kernel_page_table;


uint32_t *get_page_table(int table_index) {
  uint32_t virtual = 0xFFC00000;
  virtual |= (table_index << 12);
  if ((uint32_t *) virtual == NULL) {
    kprintf("error: page table at index %d not available\n", table_index);
    return NULL;
  }
  return (uint32_t *) virtual;
}

//
//
//

void paging_init() {
  page_directory = (uint32_t *) &_page_directory;
  kernel_page_table = (uint32_t *) &_kernel_page_table;

  // Recursively map the last entry in the page directory
  // to itself. This makes it very easy to access the page
  // directory at any time.
  page_directory[1023] = 0 | virt_to_phys(page_directory) | PE_READ_WRITE | PE_PRESENT;


  // Map the kernel (i.e first 4MB of the physical address space)
  // to a single 4MB page.
  int kernel_page = addr_to_pde(kernel_start);
  // page_directory[kernel_page] = 0x0 | PE_READ_WRITE | PE_PRESENT | PE_PAGE_SIZE_4MB;
  // kprintf("0b%08b\n", page_directory[kernel_page]);

  page_directory[kernel_page] = virt_to_phys(kernel_page_table) | PE_READ_WRITE | PE_PRESENT;

  uint32_t *kernel_pt = get_page_table(kernel_page);
  for (int i = 0; i < 1024; i++) {
    uintptr_t addr = i * 0x1000;
    kernel_pt[i] = 0 | addr | PE_READ_WRITE | PE_PRESENT;
  }

  // page_directory[0] = 0;
}

void map_page(page_t *page) {
  kprintf("mapping page of order %d (%d page frames)\n", page->order, 1 << page->order);
  // kprintf("  virtual      physical\n");

  page->flags.present = true;
  for (int i = 0; i < (1 << page->order); i++) {
    size_t offset = i * PAGE_SIZE;
    uintptr_t virt_addr = page->virt_addr + offset;
    uintptr_t phys_addr = page->phys_addr + offset;

    uint32_t pte = 0 | phys_addr | PE_PRESENT;
    if (page->flags.readwrite) {
      pte |= PE_READ_WRITE;
    }
    if (page->flags.user) {
      pte |= PE_USER_SUPERVISOR;
    }

    // kprintf("pd index: %d | pt index: %d\n", addr_to_pde(virt_addr), addr_to_pte(virt_addr));
    // kprintf("virt_addr: 0x%08X\n", virt_addr);
    // kprintf("phys_addr: 0x%08X\n", phys_addr);

    uint32_t *page_table = get_page_table(addr_to_pde(virt_addr));
    page_table[addr_to_pte(virt_addr)] = pte;

    // kprintf("0x%08X -> 0x%08X\n", virt_addr, phys_addr);
  }
}

void remap_page(page_t *page) {}

void unmap_page(page_t *page) {
  kprintf("unmapping page of order %d\n", page->order);
  kprintf("not implemented yet\n");
}
