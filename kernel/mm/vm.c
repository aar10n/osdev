//
// Created by Aaron Gill-Braun on 2020-10-03.
//

#include <base.h>
#include <panic.h>
#include <string.h>
#include <stdio.h>

#include <cpu/cpu.h>
#include <mm/mm.h>
#include <mm/heap.h>
#include <mm/vm.h>

#include <interval_tree.h>

static uint64_t *pml4;
static uint64_t *temp_dir;
static intvl_node_t *tree;

//

static inline size_t page_to_size(page_t *page) {
  if (page->flags.page_size_2mb) {
    return PAGE_SIZE_2MB;
  } else if (page->flags.page_size_1gb) {
    return PAGE_SIZE_1GB;
  }
  return PAGE_SIZE;
}

static inline uint16_t get_index(uintptr_t virt_addr, uint16_t offset, uint16_t level) {
  if (level > 4 - offset) {
    return R_ENTRY;
  }
  return (virt_addr >> page_level_to_shift(level + offset)) & 0x1FF;
}

//

uint64_t *get_table(uintptr_t virt_addr, uint16_t level) {
  uintptr_t addr = get_virt_addr(R_ENTRY, R_ENTRY, R_ENTRY, R_ENTRY);
  for (int i = 1; i < 5 - level; i++) {
    uint16_t index = get_index(virt_addr, 1, 4 - i);
    addr <<= 9;
    addr |= (0xFFFFUL << 48) | get_virt_addr_partial(index, 1);
  }
  return (uint64_t *) addr;
}

uint64_t *map_page(uintptr_t virt_addr, uintptr_t phys_addr, uint16_t flags) {
  uint16_t offset = (flags & PE_1GB_SIZE) ? 3 :
                    (flags & PE_2MB_SIZE) ? 2 : 1;

  kprintf("--- map page ---\n");
  kprintf("pml4[%d][%d][%d][%d] -> %d\n\n",
          get_index(virt_addr, offset, 4), get_index(virt_addr, offset, 3),
          get_index(virt_addr, offset, 2), get_index(virt_addr, offset, 1),
          get_index(virt_addr, offset, 0));
  kprintf("virt_addr: %p\n", virt_addr);
  kprintf("phys_addr: %p\n", phys_addr);
  kprintf("flags: %d\n", flags);

  uintptr_t addr = get_virt_addr(R_ENTRY, R_ENTRY, R_ENTRY, R_ENTRY);
  uint64_t *table = ((uint64_t *) addr);
  for (int i = 1; i < 5; i++) {
    int level = 5 - i;
    uint16_t index = get_index(virt_addr, offset, level);

    kprintf("level %d | index %d (%d)\n", i, index, level);
    if (table[index] == 0) {
      // allocate new page table
      kprintf("allocating new table (level %d)\n", level);

      page_t *page = alloc_page(PE_WRITE);
      page->flags.present = 1;

      uint64_t *new_table = get_table(virt_addr, level);
      uintptr_t new_table_ptr = (uintptr_t) new_table;

      vm_area_t *area = kmalloc(sizeof(vm_area_t));
      area->base = new_table_ptr;
      area->size = PAGE_SIZE;
      area->pages = page;

      interval_t interval = {area->base, area->base + area->size};
      tree_add_interval(tree, interval, area);

      // temporarily map the page to zero it
      temp_dir[TEMP_ENTRY] = page->frame | PE_WRITE | PE_PRESENT;
      tlb_flush();
      memset((void *) TEMP_PAGE, 0, PAGE_SIZE);

      // map the intermediate table
      table[index] = page->frame | PE_WRITE | PE_PRESENT;
    }

    addr <<= 9;
    addr |= (0xFFFFUL << 48) | get_virt_addr_partial(index, 1);
    table = ((uint64_t *) addr);
    kprintf("pml4[%d][%d][%d][%d]\n",
            PML4_INDEX(addr), PDPT_INDEX(addr),
            PDT_INDEX(addr), PT_INDEX(addr));
  }

  kprintf("final addr: %p\n", addr);

  // final offset into table
  uint16_t index = get_index(virt_addr, offset, 0);
  kprintf("final index: %d\n", index);
  table[index] = phys_addr | flags;
  return table + index;
}

//

void vm_init() {
  kprintf("[vm] initializing virtual memory manager\n");
  pml4 = (uint64_t *) boot_info->pml4;

  // clear the mappings needed by uefi
  // pml4[0] = 0;
  // recursive pml4
  pml4[R_ENTRY] = virt_to_phys((uintptr_t) pml4) | 0b11;

  // setup the page table for temporary mappings
  uintptr_t dir1 = virt_to_phys(boot_info->reserved_base - (2 * PAGE_SIZE));
  uintptr_t dir2 = virt_to_phys(boot_info->reserved_base - PAGE_SIZE);

  get_table(TEMP_PAGE, 3)[511] = dir1 | PE_WRITE | PE_PRESENT;
  get_table(TEMP_PAGE, 2)[511] = dir2 | PE_WRITE | PE_PRESENT;
  temp_dir = get_table(TEMP_PAGE, 1);

  // create the interval tree
  tree = create_interval_tree(0, UINT64_MAX);

  // recursively mapped region
  uintptr_t recurs_start = get_virt_addr(R_ENTRY, 0, 0, 0);
  uintptr_t recurs_end = get_virt_addr(R_ENTRY, 511L, 511L, 511L);

  vm_area_t *recurs_area = kmalloc(sizeof(vm_area_t));
  recurs_area->base = recurs_start;
  recurs_area->size = recurs_end - recurs_start;
  recurs_area->pages = NULL;

  interval_t recursive = { recurs_start, recurs_end };
  tree_add_interval(tree, recursive, recurs_area);

  // temporary page space
  vm_area_t *temp_area = kmalloc(sizeof(vm_area_t));
  temp_area->base = TEMP_PAGE;
  temp_area->size = PAGE_SIZE - 1;
  temp_area->pages = NULL;

  interval_t temp = { TEMP_PAGE, TEMP_PAGE + PAGE_SIZE - 1 };
  tree_add_interval(tree, temp, temp_area);

  // virtual kernel space
  vm_area_t *kernel_area = kmalloc(sizeof(vm_area_t));
  kernel_area->base = KERNEL_VA;
  kernel_area->size = KERNEL_RESERVED;
  kernel_area->pages = NULL;

  interval_t kernel = { KERNEL_VA, KERNEL_VA + KERNEL_RESERVED };
  tree_add_interval(tree, kernel, kernel_area);

  // virtual stack space
  uint64_t stack_size = boot_info->num_cores * (STACK_SIZE + 1);
  vm_area_t *stack_area = kmalloc(sizeof(vm_area_t));
  stack_area->base = STACK_VA - stack_size;
  stack_area->size = stack_size;
  stack_area->pages = NULL;

  interval_t stack = { STACK_VA - stack_size, STACK_VA };
  tree_add_interval(tree, stack, stack_area);

  // uintptr_t virt_addr = 0xFFFFFFC000000000;
  // uint16_t offset = 2;
  // kprintf("pml4[%d][%d][%d][%d] -> %d\n",
  //         get_index(virt_addr, offset, 4), get_index(virt_addr, offset, 3),
  //         get_index(virt_addr, offset, 2), get_index(virt_addr, offset, 1),
  //         get_index(virt_addr, offset, 0));
  //
  // kprintf("index 4: %d\n", get_index(virt_addr, offset, 4));
  // kprintf("index 3: %d\n", get_index(virt_addr, offset, 3));
  // kprintf("index 2: %d\n", get_index(virt_addr, offset, 2));
  // kprintf("index 1: %d\n", get_index(virt_addr, offset, 1));
  // kprintf("index 0: %d\n", get_index(virt_addr, offset, 0));
  // kprintf("\n");

  // virtual framebuffer space
  vm_map_vaddr(
    FRAMEBUFFER_VA,
    boot_info->fb_base,
    boot_info->fb_size,
    PE_WRITE | PE_PRESENT
  );

  kprintf("[vm] done!\n");
}

/**
 * Maps a physical page or pages to an available virtual address.
 */
void *vm_map_page(page_t *page) {
  size_t len = 0;
  page_t *current = page;
  while (current) {
    len += PAGE_SIZE;
    current = page->next;
  }


  interval_t interval = tree_find_free_interval(tree, len);
  if (IS_ERROR_INTERVAL(interval)) {
    panic("[vm_map_page] failed to map page - no free address\n");
  }

  interval.end = interval.start + len;
  vm_area_t *area = kmalloc(sizeof(vm_area_t));
  area->base = interval.start;
  area->size = len;
  area->pages = page;

  tree_add_interval(tree, interval, area);

  current = page;
  uintptr_t virt_addr = interval.start;
  while (current) {
    page->flags.present = 1;
    uint64_t *entry = map_page(virt_addr, page->frame, page->flags.raw);
    page->entry = entry;

    current = current->next;
    virt_addr += page_to_size(current);
  }

  return (void *) interval.start;
}

/**
 * Maps the specified region to an available virtual address.
 */
void *vm_map_addr(uintptr_t phys_addr, size_t len, uint16_t flags) {
  interval_t interval = tree_find_free_interval(tree, len);
  if (IS_ERROR_INTERVAL(interval)) {
    panic("[vm_map_addr] failed to map address - no free address\n");
  }

  interval.end = interval.start + len;
  vm_area_t *area = kmalloc(sizeof(vm_area_t));
  area->base = interval.start;
  area->size = len;
  area->pages = NULL;

  tree_add_interval(tree, interval, area);
  vm_map_vaddr(interval.start, phys_addr, len, flags | PE_PRESENT);

  return (void *) interval.start;
}

/**
 * Maps the given virtual address to the specified region.
 */
void *vm_map_vaddr(uintptr_t virt_addr, uintptr_t phys_addr, size_t len, uint16_t flags) {
  interval_t interval = {virt_addr, virt_addr + len };

  vm_area_t *area = kmalloc(sizeof(vm_area_t));
  area->base = interval.start;
  area->size = len;
  area->pages = NULL;

  bool success = tree_add_interval(tree, interval, area);
  if (!success) {
    panic("[vm_map_vaddr] failed to map address - already in use\n");
  }

  // add table entry
  size_t remaining = len;
  while (remaining > 0) {
    size_t size = PAGE_SIZE;
    uint16_t cur_flags = flags;
    if (remaining >= PAGE_SIZE_1GB) {
      size = PAGE_SIZE_1GB;
      cur_flags |= PE_SIZE | PE_1GB_SIZE;
    } else if (remaining >= PAGE_SIZE_2MB) {
      size = PAGE_SIZE_2MB;
      cur_flags |= PE_SIZE | PE_2MB_SIZE;
    }

    map_page(virt_addr, phys_addr, cur_flags);

    remaining -= size;
    virt_addr += size;
    phys_addr += size;
  }

  return (void *) interval.start;
}
