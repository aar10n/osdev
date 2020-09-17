//
// Created by Aaron Gill-Braun on 2019-06-06.
//

#include <stdio.h>
#include <string.h>

#include <kernel/cpu/asm.h>
#include <kernel/mem/heap.h>
#include <kernel/mem/mm.h>
#include <kernel/mem/paging.h>

extern uintptr_t _kernel_directory;
pde_t *kernel_pd;

extern uintptr_t _first_page_table;
pte_t *first_pt;


/* ----- Page Table Functions ----- */

pte_t *create_page_table() {
  pte_t *pt = kmalloc(sizeof(pte_t) * 1024);
  memset(pt, 0, sizeof(pte_t) * 1024);
  return pt;
}

pte_t *get_page_table(int index) {
  pde_t *pde = current_pd;
  if (pde[index] == 0) {
    return NULL;
  }
  return pde_to_pt(pde[index]);
}

pte_t *clone_page_table(const pte_t *src) {
  pte_t *dest = create_page_table();
  for (int i = 0; i < 1024; i++) {
    pte_t src_pte = src[i];
    if (!src_pte) continue;

    page_t *page = alloc_page(0);
    dest[i] = page->phys_addr | (src_pte & PE_FLAGS);

    copy_page_frame(src_pte & PE_ADDRESS, page->phys_addr);
  }
  return dest;
}

/* ----- Page Directory Functions ----- */

pde_t *create_page_directory() {
  pde_t *pd = kmalloc(sizeof(pde_t) * 1024);
  memset(pd, 0, sizeof(pde_t) * 1024);

  // map last entry to the directory itself
  pd[1023] = virt_to_phys(pd) | PE_READ_WRITE | PE_PRESENT;
  return pd;
}

pde_t *clone_page_directory(const pde_t *src) {
  pde_t *dest = create_page_directory();
  for (int i = 0; i < 1023; i++) {
    pde_t src_pde = src[i];
    if (!src_pde) continue;

    if (&src[i] == &kernel_pd[i] || entry_flag(src_pde, PDE_PAGE_SIZE)) {
      dest[i] = src_pde;
    } else {
      pte_t *src_table = pde_to_pt(src_pde);
      pte_t *dest_table = clone_page_table(src_table);
      dest[i] = virt_to_phys(dest_table) | (src_pde & PE_FLAGS);
    }
  }
  return dest;
}

//
//
//

void paging_init() {
  kernel_pd = (pde_t *) &_kernel_directory;
  first_pt = (pde_t *) &_first_page_table;

  // Identity map the first 4mb except for the very first page
  // this ensures that any null pointer accesses cause a page
  // fault
  for (int i = 1; i < 1024; i++) {
    first_pt[i] = (i << 22) | PE_READ_WRITE | PE_PRESENT;
  }

  kernel_pd[0] = virt_to_phys(first_pt) | PE_PRESENT | PE_READ_WRITE;

  // Recursively map the last entry in the page directory
  // to itself. This makes it very easy to access the page
  // directory at any time.
  kernel_pd[1023] = virt_to_phys(kernel_pd) | PE_READ_WRITE | PE_PRESENT;

  // Map the kernel (i.e first 4MB of the physical address space) to 3GB
  int kernel_page = addr_to_pde(kernel_start);
  kernel_pd[kernel_page] = 0 | PDE_PAGE_SIZE | PE_PRESENT;

  // Map in the last 1GB ram as kernel space
  for (int i = kernel_page + 1; i < 1023; i++) {
    uint32_t addr = i << 22;
    pde_t pde = virt_to_phys(addr) | PDE_PAGE_SIZE | PE_READ_WRITE | PE_PRESENT;
    kernel_pd[i] = pde;
  }
}

/* ----- Map Page Frame ----- */

void map_frame(uintptr_t virt_addr, pte_t pte) {
  int index = addr_to_pde(virt_addr);
  pte_t *page_table = get_page_table(index);
  if (page_table == NULL) {
    kprintf("allocating new page table\n");
    // create the table
    pde_t *pd = current_pd;
    page_table = create_page_table();

    pd[index] = virt_to_phys(page_table)
                | entry_flag(pte, PE_READ_WRITE)
                | entry_flag(pte, PE_USER)
                | entry_flag(pte, PE_CACHE_DISABLED)
                | PE_PRESENT;
  }

  page_table[addr_to_pte(virt_addr)] = pte;
}

void map_page(page_t *page) {
  // kprintf("mapping page of order %d (%d page frames)\n", page->order, 1 << page->order);
  // kprintf("  virtual      physical\n");
  // kprintf("%08p -> %08p\n", page->virt_addr, page->phys_addr);
  page->flags.present = true;

  for (int i = 0; i < (1 << page->order); i++) {
    size_t offset = i * PAGE_SIZE;
    uintptr_t virt_addr = page->virt_addr + offset;
    uintptr_t phys_addr = page->phys_addr + offset;

    pte_t pte = phys_addr | PE_PRESENT;
    if (page->flags.readwrite) {
      pte |= PE_READ_WRITE;
    }
    if (page->flags.user) {
      pte |= PE_USER;
    }

    // kprintf("pd index: %d | pt index: %d\n", addr_to_pde(virt_addr), addr_to_pte(virt_addr));
    // kprintf("virt_addr: 0x%08X\n", virt_addr);
    // kprintf("phys_addr: 0x%08X\n", phys_addr);

    // kprintf("0x%08X -> 0x%08X\n", virt_addr, phys_addr);
    map_frame(virt_addr, pte);
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

