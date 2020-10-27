//
// Created by Aaron Gill-Braun on 2020-10-03.
//

#include <base.h>
#include <panic.h>
#include <string.h>
#include <stdio.h>
#include <percpu.h>

#include <cpu/cpu.h>
#include <mm/mm.h>
#include <mm/heap.h>
#include <mm/vm.h>

#include <interval_tree.h>

// static uint64_t *pml4;
// static uint64_t *temp_dir;
// static intvl_tree_t *tree;

//

static inline size_t page_to_size(page_t *page) {
  if (page->flags.page_size_2mb) {
    return PAGE_SIZE_2MB;
  } else if (page->flags.page_size_1gb) {
    return PAGE_SIZE_1GB;
  }
  return PAGE_SIZE;
}

static inline uint16_t page_to_flags(page_t *page) {
  return page->flags.raw & 0xFFF;
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
  flags &= 0xFFF;
  uint16_t offset = (flags & PE_1GB_SIZE) ? 3 :
                    (flags & PE_2MB_SIZE) ? 2 : 1;

  // kprintf("--- map page ---\n");
  // kprintf("pml4[%d][%d][%d][%d] -> %d\n\n",
  //         get_index(virt_addr, offset, 4), get_index(virt_addr, offset, 3),
  //         get_index(virt_addr, offset, 2), get_index(virt_addr, offset, 1),
  //         get_index(virt_addr, offset, 0));
  // kprintf("virt_addr: %p\n", virt_addr);
  // kprintf("phys_addr: %p\n", phys_addr);
  // kprintf("flags: %d\n", flags);

  uintptr_t addr = get_virt_addr(R_ENTRY, R_ENTRY, R_ENTRY, R_ENTRY);
  uint64_t *table = ((uint64_t *) addr);
  for (int i = 1; i < 5; i++) {
    int level = 5 - i;
    uint16_t index = get_index(virt_addr, offset, level);

    // kprintf("level %d | index %d (%d)\n", i, index, level);
    if (table[index] == 0) {
      // allocate new page table
      // kprintf("allocating new table (level %d)\n", level);
      // kprintf("[vm] allocating page table\n");

      page_t *page = mm_alloc_page(ZONE_LOW, PE_WRITE);
      page->flags.present = 1;

      // kprintf("[vm] page table: %p\n", page->frame);

      uint64_t *new_table = get_table(virt_addr, level);
      uintptr_t new_table_ptr = (uintptr_t) new_table;

      vm_area_t *area = kmalloc(sizeof(vm_area_t));
      area->base = new_table_ptr;
      area->size = PAGE_SIZE;
      area->pages = page;

      interval_t interval = {area->base, area->base + area->size};
      intvl_tree_insert(VM->tree, interval, area);

      // temporarily map the page to zero it
      VM->temp_dir[TEMP_ENTRY] = page->frame | PE_WRITE | PE_PRESENT;
      tlb_flush();
      memset((void *) TEMP_PAGE, 0, PAGE_SIZE);

      // map the intermediate table
      table[index] = page->frame | PE_WRITE | PE_PRESENT;
    }

    addr <<= 9;
    addr |= (0xFFFFUL << 48) | get_virt_addr_partial(index, 1);
    table = ((uint64_t *) addr);
    // kprintf("pml4[%d][%d][%d][%d]\n",
    //         PML4_INDEX(addr), PDPT_INDEX(addr),
    //         PDT_INDEX(addr), PT_INDEX(addr));
  }

  // kprintf("final addr: %p\n", addr);

  // final offset into table
  uint16_t index = get_index(virt_addr, offset, 0);
  // kprintf("final index: %d\n", index);
  table[index] = phys_addr | flags;

  // kprintf("[vm] %p -> %p\n", phys_addr, virt_addr);

  tlb_flush();
  return table + index;
}

//

void vm_init() {
  kprintf("[vm] initializing\n");
  vm_t *vm = kmalloc(sizeof(vm_t));
  vm->tree = create_intvl_tree();
  vm->temp_dir = NULL;
  VM = vm;

  uint64_t *pml4 = NULL;
  if (IS_BSP) {
    pml4 = (uint64_t *) boot_info->pml4;
    vm->pml4 = pml4;
    // only the bsp has to set up recursive pml4 mappings
    // because the AP kernel tables come pre-setup as
    // recursive page tables
    pml4[R_ENTRY] = virt_to_phys((uintptr_t) pml4) | PE_WRITE | PE_PRESENT;
    tlb_flush();
  } else {
    // for the APs we can get the virtual address of the
    // pml4 through the recursive mappings
    pml4 = get_table(0, 4);
    vm->pml4 = pml4;
  }

  // setup the page table for temporary mappings
  uintptr_t dir1 = virt_to_phys(boot_info->reserved_base - PAGES_TO_SIZE(2));
  uintptr_t dir2 = virt_to_phys(boot_info->reserved_base - PAGES_TO_SIZE(1));

  get_table(TEMP_PAGE, 3)[511] = dir1 | PE_WRITE | PE_PRESENT;
  get_table(TEMP_PAGE, 2)[511] = dir2 | PE_WRITE | PE_PRESENT;
  vm->temp_dir = get_table(TEMP_PAGE, 1);
  tlb_flush();

  //
  // Virtual Address Space Layout
  //

  // null page (fault on null reference)
  intvl_tree_insert(vm->tree, intvl(0, PAGE_SIZE), NULL);
  // non-canonical address space
  intvl_tree_insert(vm->tree, intvl(LOW_HALF_END + 1, HIGH_HALF_START), NULL);
  // recursively mapped region
  uintptr_t recurs_start = get_virt_addr(R_ENTRY, 0, 0, 0);
  uintptr_t recurs_end = get_virt_addr(R_ENTRY, K_ENTRY, K_ENTRY, K_ENTRY);
  intvl_tree_insert(vm->tree, intvl(recurs_start, recurs_end), NULL);
  // temporary page space
  interval_t temp = { TEMP_PAGE, HIGH_HALF_END };
  intvl_tree_insert(vm->tree, temp, NULL);
  // virtual kernel space
  uintptr_t kernel_start = KERNEL_VA;
  uintptr_t kernel_end = KERNEL_VA + KERNEL_RESERVED;
  intvl_tree_insert(vm->tree, intvl(kernel_start, kernel_end), NULL);
  // virtual stack space
  uint64_t logical_cores = boot_info->num_cores * boot_info->num_threads;
  uint64_t stack_size = logical_cores * (STACK_SIZE + 1);
  uintptr_t stack_start = STACK_VA - stack_size;
  uintptr_t stack_end = STACK_VA;
  intvl_tree_insert(vm->tree, intvl(stack_start, stack_end), NULL);

  // framebuffer
  vm_map_vaddr(
    FRAMEBUFFER_VA,
    boot_info->fb_base,
    boot_info->fb_size,
    PE_WRITE | PE_PRESENT
  );

  // finally clear the uefi or bsp created
  // identity mappings
  pml4[0] = 0;
  tlb_flush();

  kprintf("[vm] done!\n");
}

/**
 * Creates kernel page tables for an AP.
 */
void *vm_create_ap_tables() {
  // since these tables are also used when switching
  // from protected to long mode, the tables need to
  // be within the first 4GB of ram.
  page_t *ap_pml4_page = mm_alloc_page(ZONE_NORMAL, PE_WRITE | PE_ASSERT);
  page_t *low_pdpt_page = mm_alloc_page(ZONE_NORMAL, PE_WRITE | PE_ASSERT);

  uint64_t *ap_pml4 = vm_map_page(ap_pml4_page);
  uint64_t *low_pdpt = vm_map_page(low_pdpt_page);

  memset(ap_pml4, 0, PAGE_SIZE);
  memset(low_pdpt, 0, PAGE_SIZE);

  ap_pml4[0] = (((uintptr_t) low_pdpt_page->frame) | PE_WRITE | PE_PRESENT);
  ap_pml4[K_ENTRY] = VM->pml4[K_ENTRY]; // copy bsp upper tables
  // recursive pml4
  ap_pml4[R_ENTRY] = ap_pml4_page->frame | PE_WRITE | PE_PRESENT;

  // first 1gb identity mapped
  low_pdpt[0] = (0 | PE_SIZE | PE_WRITE | PE_PRESENT);
  return (void *) ap_pml4_page->frame;
}

/**
 * Maps a physical page or pages to an available virtual address.
 */
void *vm_map_page(page_t *page) {
  size_t len = 0;
  page_t *current = page;
  while (current) {
    len += page_to_size(current);
    current = page->next;
  }

  uintptr_t address = 0;
  bool success = vm_find_free_area(ABOVE, &address, len);
  if (!success) {
    panic("[vm] no free address space");
  }

  return vm_map_page_vaddr(address, page);
}

/**
 * Maps a physical page to the specified virtual address.
 */
void *vm_map_page_vaddr(uintptr_t virt_addr, page_t *page) {
  uintptr_t address = virt_addr;
  size_t len = 0;
  page_t *current = page;
  while (current) {
    len += page_to_size(current);
    current = page->next;
  }

  vm_area_t *area = kmalloc(sizeof(vm_area_t));
  area->base = address;
  area->size = len;
  area->pages = page;
  intvl_tree_insert(VM->tree, intvl(address, address + len), area);

  current = page;
  while (current) {
    page->flags.present = 1;
    uint64_t *entry = map_page(virt_addr, page->frame, page_to_flags(page));
    page->entry = entry;

    virt_addr += page_to_size(current);
    current = current->next;
  }

  return (void *) address;
}

/**
 * Maps the specified region to an available virtual address.
 */
void *vm_map_addr(uintptr_t phys_addr, size_t len, uint16_t flags) {
  len = align(len, PAGE_SIZE);
  uintptr_t address = 0;
  bool success = vm_find_free_area(ABOVE, &address, len);
  if (!success) {
    panic("[vm] no free address space");
  }

  vm_area_t *area = kmalloc(sizeof(vm_area_t));
  area->base = address;
  area->size = len;
  area->pages = NULL;
  intvl_tree_insert(VM->tree, intvl(address, len), area);

  vm_map_vaddr(address, phys_addr, len, flags | PE_PRESENT);
  return (void *) address;
}

/**
 * Maps the given virtual address to the specified region.
 */
void *vm_map_vaddr(uintptr_t virt_addr, uintptr_t phys_addr, size_t len, uint16_t flags) {
  len = align(len, PAGE_SIZE);
  interval_t interval = intvl(virt_addr, virt_addr + len);
  intvl_node_t *existing = intvl_tree_find(VM->tree, interval);
  if (existing) {
    panic("[vm] failed to map address - already in use\n");
  }

  vm_area_t *area = kmalloc(sizeof(vm_area_t));
  area->base = interval.start;
  area->size = len;
  area->pages = NULL;

  intvl_tree_insert(VM->tree, interval, area);

  // add table entry
  size_t remaining = len;
  while (remaining > 0) {
    size_t size = PAGE_SIZE;
    uint16_t cur_flags = flags | PE_PRESENT;
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

/**
 * Returns the vm_area struct associated with the given
 * address, or `NULL` if one does not exist.
 */
vm_area_t *vm_get_vm_area(uintptr_t address) {
  intvl_node_t *node = intvl_tree_find(VM->tree, intvl(address, address + 1));
  if (node == NULL) {
    return NULL;
  }
  return node->data;
}

/**
 * Looks for an available address space of the given size
 * using the provided search parameters. If no such address
 * could be found, `false` is returned and `addr` is undefined.
 */
bool vm_find_free_area(vm_search_t search_type, uintptr_t *addr, size_t size) {
  uintptr_t ptr = *addr;

  interval_t interval = intvl(ptr, ptr + size);
  intvl_node_t *closest = intvl_tree_find_closest(VM->tree, interval);
  if (search_type == EXACTLY) {
    if (overlaps(interval, closest->interval)) {
      return false;
    }
    return true;
  }

  rb_iter_type_t iter_type;
  if (search_type == ABOVE) {
    iter_type = FORWARD;
  } else {
    iter_type = REVERSE;
  }

  rb_node_t *node = NULL;
  rb_node_t *last = NULL;
  rb_iter_t *iter = rb_tree_make_iter(VM->tree->tree, closest->node, iter_type);
  while ((node = rb_iter_next(iter))) {
    interval_t i = ((intvl_node_t *) node->data)->interval;
    interval_t j = last ? ((intvl_node_t *) last->data)->interval : i;

    bool contig = contiguous(i, j);
    if (search_type == ABOVE) {
      if (!contig && i.start > ptr && i.start - ptr >= size) {
        *addr = ptr;
        return true;
      }
      ptr = i.end;
    } else {
      if (!contig && i.end < ptr && ptr - i.end >= size) {
        *addr = ptr;
        return true;
      }
      ptr = i.start - 1;
    }

    last = node;
  }
  return false;
}

