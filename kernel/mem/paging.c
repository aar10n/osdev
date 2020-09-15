//
// Created by Aaron Gill-Braun on 2019-06-06.
//

#include <stdio.h>
#include <string.h>

#include <kernel/cpu/asm.h>
#include <kernel/mem/heap.h>
#include <kernel/mem/mm.h>
#include <kernel/mem/paging.h>

extern uint32_t _page_directory;
page_directory_t *kernel_directory;
page_directory_t *current_directory;

extern uint32_t _first_page_table;
static page_table_t *first_page_table;


/* ----- Page Table Functions ----- */

page_table_t *create_page_table() {
  page_table_t *pt = kmalloc(sizeof(page_table_t));
  memset(pt, 0, sizeof(page_table_t));
  return pt;
}

page_table_t *get_page_table(int index) {
  uint32_t virtual = 0xFFC00000;
  virtual |= (index << 12);
  if ((uint32_t *) virtual == NULL) {
    return NULL;
  }
  return (page_table_t *) virtual;
}

page_table_t *clone_page_table(page_table_t *src) {
  page_table_t *dest = create_page_table();
  for (int i = 0; i < 1024; i++) {
    pte_t *src_pte = src[i];
    pte_t *dest_pte = dest[i];
    if (!src_pte) continue;

    page_t *page = alloc_page(0);
    dest_pte->raw = src_pte->raw;
    dest_pte->page_frame = page->phys_addr;
    copy_page_frame(src_pte->page_frame, dest_pte->page_frame);
  }
  return dest;
}

/* ----- Page Directory Functions ----- */

page_directory_t *create_page_directory() {
  page_directory_t *pd = kmalloc(sizeof(page_directory_t));
  memset(pd, 0, sizeof(page_directory_t));

  // map last entry to the directory itself
  pde_t *last = pd[1023];
  last->present = 1;
  last->read_write = 1;
  last->page_table = virt_to_phys(pd);
  return pd;
}

page_directory_t *clone_page_directory(page_directory_t *src) {
  page_directory_t *dest = create_page_directory();
  for (int i = 0; i < 1023; i++) {
    pde_t *src_pde = src[i];
    pde_t *dest_pde = dest[i];
    if (!src_pde) continue;

    if (src_pde == kernel_directory[i] || src_pde->page_size) {
      dest_pde->raw = src_pde->raw;
    } else {
      page_table_t *src_table = (void *) src_pde->page_table;
      page_table_t *dest_table = clone_page_table(src_table);
      dest_pde->raw = src_pde->raw;
      dest_pde->page_table = virt_to_phys(dest_table);
    }
  }
  return dest;
}


//
//
//

void paging_init() {
  kernel_directory = (page_directory_t *) &_page_directory;
  first_page_table = (page_table_t *) &_first_page_table;

  // Identity map the first 4mb except for the very first page
  // this ensures that any null pointer dereferencing causes a
  // page fault
  for (int i = 1; i < 1024; i++) {
    uint32_t addr = i << 22;
    pte_t *pte = first_page_table[i];
    pte->present = 1;
    pte->read_write = 1;
    pte->page_frame = addr;
  }

  pde_t *first = kernel_directory[0];
  first->present = 1;
  first->read_write = 1;
  first->page_table = virt_to_phys(first_page_table);

  // Recursively map the last entry in the page directory
  // to itself. This makes it very easy to access the page
  // directory at any time.
  pde_t *last = kernel_directory[1023];
  last->present = 1;
  last->read_write = 1;
  last->page_table = virt_to_phys(kernel_directory);

  // Map the kernel (i.e first 4MB of the physical address space) to 3GB
  int kernel_page = addr_to_pde(kernel_start);
  kernel_directory[kernel_page]->present = 1;
  kernel_directory[kernel_page]->page_size = 1;

  // Map remainder of the last 1GB ram as kernel space
  for (int i = kernel_page + 1; i < 1023; i++) {
    uint32_t addr = i << 22;
    pde_t *pde = kernel_directory[i];
    pde->present = 1;
    pde->read_write = 1;
    pde->page_size = 1;
    pde->page_table = virt_to_phys(addr);
  }

  current_directory = kernel_directory;
}

/* ----- Map Page Frame ----- */

void map_frame(uintptr_t virt_addr, pte_t pte) {
  int index = addr_to_pde(virt_addr);
  page_table_t *page_table = get_page_table(index);
  if (page_table == NULL) {
    kprintf("allocating new page table\n");
    // create the table
    page_directory_t *directory = (void *) 0xFFC00000;
    page_table = create_page_table();

    pde_t pde;
    pde.present = 1;
    pde.read_write = pte.read_write;
    pde.user = pte.user;
    pde.cache_disable = pte.cache_disable;
    pde.page_table = virt_to_phys(page_table);
    directory[index]->raw = pde.raw;
  }

  page_table[addr_to_pte(virt_addr)]->raw = pte.raw;
}

void map_page(page_t *page) {
  // kprintf("mapping page of order %d (%d page frames)\n", page->order, 1 << page->order);
  // kprintf("  virtual      physical\n");
  // kprintf("%08p -> %08p\n", page->virt_addr, page->phys_addr);
  page->flags.present = true;

  pte_t pte;
  pte.raw = 0;
  for (int i = 0; i < (1 << page->order); i++) {
    size_t offset = i * PAGE_SIZE;
    uintptr_t virt_addr = page->virt_addr + offset;
    uintptr_t phys_addr = page->phys_addr + offset;

    // uint32_t pte = 0 | phys_addr | PE_PRESENT;
    pte.present = 1;
    pte.page_frame = phys_addr;
    if (page->flags.readwrite) {
      pte.read_write = 1;
    }
    if (page->flags.user) {
      pte.user = 1;
    }

    // kprintf("pd index: %d | pt index: %d\n", addr_to_pde(virt_addr), addr_to_pte(virt_addr));
    // kprintf("virt_addr: 0x%08X\n", virt_addr);
    // kprintf("phys_addr: 0x%08X\n", phys_addr);

    map_frame(virt_addr, pte);
    // kprintf("0x%08X -> 0x%08X\n", virt_addr, phys_addr);
  }
}

/* ----- Remap Page Frame ----- */

void remap_page(page_t *page) {}


/* ----- Unmap Page Frame ----- */

void unmap_page(page_t *page) {
  kprintf("unmapping page of order %d\n", page->order);
  kprintf("not implemented yet\n");
}

//

