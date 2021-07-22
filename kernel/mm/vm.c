#include <acpi.h>
//
// Created by Aaron Gill-Braun on 2020-10-03.
//

#include <base.h>
#include <panic.h>
#include <string.h>
#include <printf.h>
#include <percpu.h>
#include <vectors.h>

#include <cpu/cpu.h>
#include <cpu/idt.h>
#include <mm/mm.h>
#include <mm/heap.h>
#include <mm/vm.h>

#include <interval_tree.h>

#define kernel_virt_to_phys(x) (((uintptr_t)(x)) - KERNEL_OFFSET)

extern void page_fault_handler();

static inline vm_area_t *alloc_area(uintptr_t base, size_t size, uint32_t attr) {
  vm_area_t *area = kmalloc(sizeof(vm_area_t));
  area->base = base;
  area->size = size;
  area->data = 0;
  area->attr = attr;
  return area;
}

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

static inline uint16_t fix_flags(uint16_t flags, uint16_t root, bool cow) {
  if (root == U_ENTRY) {
    if (cow) {
      // kprintf("setting page to read only\n");
      flags &= ~(PE_WRITE);
    }
    return flags | PE_USER;
  }
  return flags;
}

static inline void update_page_flags(page_t *page, uint16_t flags) {
  bool is_alt_size = (flags & PE_2MB_SIZE) || (flags & PE_1GB_SIZE);

  page->flags.raw = 0;
  page->flags.present = (flags & PE_PRESENT) != 0;
  page->flags.write = (flags & PE_WRITE) != 0;
  page->flags.user = (flags & PE_USER) != 0;
  page->flags.write_through = (flags & PE_WRITE_THROUGH) != 0;
  page->flags.cache_disable = (flags & PE_CACHE_DISABLE) != 0;
  page->flags.page_size = is_alt_size;
  page->flags.global = (flags & PE_GLOBAL) != 0;
  page->flags.executable = (flags & PE_EXEC) != 0;
  page->flags.page_size_2mb = (flags & PE_2MB_SIZE) != 0;
  page->flags.page_size_1gb = (flags & PE_1GB_SIZE) != 0;
}

//

uint64_t *get_table(uintptr_t virt_addr, uint16_t level) {
  uintptr_t addr = get_virt_addr(R_ENTRY, R_ENTRY, R_ENTRY, R_ENTRY);
  bool last_exists = true;
  for (int i = 1; i < 5 - level; i++) {
    uint16_t index = get_index(virt_addr, 1, 4 - i);

    if (last_exists) {
      uint64_t entry = ((uint64_t *) addr)[index];
      if (entry & PE_SIZE) {
        return (uint64_t *) addr;
      } else if (entry == 0) {
        last_exists = false;
      }
    }

    addr <<= 9;
    addr |= (0xFFFFUL << 48) | get_virt_addr_partial(index, 1);
  }
  return (uint64_t *) addr;
}

uintptr_t get_frame(uintptr_t virt_addr) {
  uintptr_t addr = get_virt_addr(R_ENTRY, R_ENTRY, R_ENTRY, R_ENTRY);
  for (int i = 1; i < 4; i++) {
    uint16_t index = get_index(virt_addr, 1, 4 - i);

    uint64_t entry = ((uint64_t *) addr)[index];
    // kprintf("get_frame | entry: %#b\n", entry & PAGE_FLAGS_MASK);
    if (entry & PE_SIZE) {
      return entry & PAGE_FRAME_MASK;
    }

    addr <<= 9;
    addr |= (0xFFFFUL << 48) | get_virt_addr_partial(index, 1);
  }

  uint16_t index = get_index(virt_addr, 1, 0);
  uint64_t entry = ((uint64_t *) addr)[index];
  // kprintf("last entry: %p | %#b\n", entry & PAGE_FRAME_MASK, entry & PAGE_FLAGS_MASK);
  return entry & PAGE_FRAME_MASK;
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

  int root = -1;
  uintptr_t addr = get_virt_addr(R_ENTRY, R_ENTRY, R_ENTRY, R_ENTRY);
  uint64_t *table = ((uint64_t *) addr);
  for (int i = 1; i < 5; i++) {
    int level = 5 - i;
    uint16_t index = get_index(virt_addr, offset, level);
    if (root == -1 && index != R_ENTRY) {
      root = index;
    }

    // kprintf("level %d | index %d (%d)\n", i, index, level);
    if ((table[index] & PAGE_FRAME_MASK) == 0) {
      // allocate new page table
      // kprintf("allocating new table (level %d)\n", level);
      // kprintf("[vm] allocating page table\n");

      page_t *page = alloc_frame(PE_WRITE);
      page->flags.present = 1;

      // kprintf("[vm] page table: %p\n", page->frame);
      // temporarily map the page to zero it
      VM->temp_dir[TEMP_ENTRY] = page->frame | PE_WRITE | PE_PRESENT;
      tlb_flush();
      memset((void *) TEMP_PAGE, 0, PAGE_SIZE);

      // map the intermediate table
      table[index] = page->frame | fix_flags(PE_WRITE | PE_PRESENT, root, false);
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
  table[index] = phys_addr | fix_flags(flags, root, false);

  // kprintf("[vm] %p -> %p\n", phys_addr, virt_addr);

  tlb_flush();
  return table + index;
}

uint64_t *copy_table(const uint64_t *table, uint16_t level, uint16_t root) {
  page_t *page = alloc_frame(PE_WRITE);
  uint64_t *new_table = vm_map_page_search(page, ABOVE, STACK_VA);
  memset(new_table, 0, PAGE_SIZE);

  uintptr_t base = (uintptr_t) table;
  for (int i = 0; i < 512; i++) {
    if (level == 4 && i == R_ENTRY) {
      // skip the pml4 recursive entry
      new_table[R_ENTRY] = page->frame | (table[i] & PAGE_FRAME_MASK);
      continue;
    } else if (level == 4 && i == K_ENTRY) {
      new_table[K_ENTRY] = table[K_ENTRY];
      continue;
    }

    if (level > 1 && (table[i] & PE_PRESENT)) {
      uintptr_t next = (0xFFFFUL << 48) | (base << 9) | (i << 12);
      // kprintf("%d (%d) | %p\n", level, i, next);

      uint16_t new_root = level == 4 ? i : root;
      uint64_t *copy = copy_table((uint64_t *) next, level - 1, new_root);
      uint16_t page_flags = table[i] & PAGE_FLAGS_MASK;
      if (page_flags & PE_SIZE) {
        // even if we're not at the lowest level
        // we treat larger page sizes as if they
        // were a bottom level entry
        uint64_t page_frame = table[i] & PAGE_FRAME_MASK;
        new_table[i] = page_frame | fix_flags(page_flags, root, true);
        // new_table[i] = table[i];
        // kprintf("[mapping] %d (%d) | root: %d | frame: %p | flags: 0b%b\n", level, i, root,
        //         new_table[i] & PAGE_FRAME_MASK, new_table[i] & PAGE_FLAGS_MASK);
        continue;
      }

      uintptr_t frame = get_frame((uintptr_t) copy);
      // new_table[i] = frame | fix_flags(page_flags, root, true);
      new_table[i] = frame | page_flags;
      // kprintf("[mapping] %d (%d) | root: %d | frame: %p | flags: 0b%b\n", level, i, root,
      //         new_table[i] & PAGE_FRAME_MASK, new_table[i] & PAGE_FLAGS_MASK);
    } else {
      uint64_t page_frame = table[i] & PAGE_FRAME_MASK;
      uint16_t page_flags = table[i] & PAGE_FLAGS_MASK;
      // new_table[i] = table[i];
      if (page_frame != 0) {
        new_table[i] = page_frame | fix_flags(page_flags, root, true);
      } else {
        new_table[i] = table[i];
      }
      // new_table[i] = table[i];
      // if ((new_table[i] & PAGE_FRAME_MASK) != 0) {
        // kprintf("-> else\n");
        // kprintf("[mapping] %d (%d) | root: %d | frame: %p | flags: 0b%b\n", level, i, root,
        //         new_table[i] & PAGE_FRAME_MASK, new_table[i] & PAGE_FLAGS_MASK);
      // }
    }
  }

  return new_table;
}

void *duplicate_intvl_node(void *data) {
  vm_area_t *area = data;
  if (data == NULL) {
    return NULL;
  }

  vm_area_t *new_area = kmalloc(sizeof(vm_area_t));
  memcpy(new_area, area, sizeof(vm_area_t));
  return new_area;
}

void *map_area(vm_area_t *area, uint16_t flags) {
  uintptr_t virt_addr = area->base;

  if (area->attr & AREA_PAGE) {
    kassert(area->pages != NULL);
    page_t *curr = area->pages;
    while (curr) {
      curr->flags.present = 1;
      uint64_t *entry = map_page(virt_addr, curr->frame, page_to_flags(curr));
      curr->addr = virt_addr;
      curr->entry = entry;

      virt_addr += page_to_size(curr);
      curr = curr->next;
    }
  } else if (area->attr & AREA_PHYS) {
    kassert(area->phys != 0);
    uintptr_t phys_addr = area->phys;
    size_t remaining = area->size;
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
  }

  intvl_tree_insert(VM->tree, intvl(area->base, area->base + area->size), area);
  return (void *) area->base;
}

//

__used int fault_handler(uintptr_t addr, uint32_t err) {
  kprintf("[vm] page fault at %p (0b%b)\n", addr, err);
  // kprintf("err: %d | err: %d\n", err & PF_PRESENT);
  if (err & PF_WRITE) {
    // copy-on-write
    kprintf("[vm] copy on write\n");

    return -1;
  }
  return -1;
}

//

void vm_init() {
  kprintf("[vm] initializing\n");

  vm_t *vm = kmalloc(sizeof(vm_t));
  intvl_tree_events_t *events = kmalloc(sizeof(intvl_tree_events_t));
  events->copy_data = duplicate_intvl_node;

  vm->tree = create_intvl_tree();
  vm->tree->events = events;
  vm->temp_dir = NULL;
  VM = vm;

  idt_gate_t handler = gate((uintptr_t) page_fault_handler, KERNEL_CS, 0, INTERRUPT_GATE, 0, 1);
  idt_set_gate(VECTOR_PAGE_FAULT, handler);

  uint64_t *pml4 = NULL;
  if (IS_BSP) {
    pml4 = (uint64_t *) boot_info->pml4;
    vm->pml4 = pml4;
    // only the bsp has to set up recursive pml4 mappings
    // because the AP kernel tables come pre-setup as
    // recursive page tables
    pml4[R_ENTRY] = kernel_virt_to_phys((uintptr_t) pml4) | PE_WRITE | PE_PRESENT;
    tlb_flush();

    // setup the page table for temporary mappings
    uint64_t *dir1 = (void *) (boot_info->reserved_base - PAGES_TO_SIZE(2));
    uint64_t *dir2 = (void *) (boot_info->reserved_base - PAGES_TO_SIZE(1));

    memset(dir1, 0, PAGE_SIZE);
    memset(dir2, 0, PAGE_SIZE);

    get_table(TEMP_PAGE, 3)[511] = kernel_virt_to_phys((uintptr_t) dir1) | PE_WRITE | PE_PRESENT;
    get_table(TEMP_PAGE, 2)[511] = kernel_virt_to_phys((uintptr_t) dir2) | PE_WRITE | PE_PRESENT;
    vm->temp_dir = get_table(TEMP_PAGE, 1);
    tlb_flush();
  } else {
    // for the APs we can get the virtual address of the
    // pml4 and temp_dir through the recursive mappings
    pml4 = get_table(0, 4);
    vm->pml4 = pml4;
    vm->temp_dir = get_table(TEMP_PAGE, 1);
    tlb_flush();
  }

  //
  // Virtual Address Space Layout
  //

  // null page (fault on null reference)
  vm_area_t *null_area = alloc_area(0, PAGE_SIZE, AREA_USED | AREA_PHYS);
  intvl_tree_insert(vm->tree, intvl(0, PAGE_SIZE), null_area);

  // non-canonical address space
  uintptr_t non_cann_start = LOW_HALF_END + 1;
  uintptr_t non_cann_end = HIGH_HALF_START;
  size_t non_cann_size = non_cann_end - non_cann_start;
  vm_area_t *non_cann_area = alloc_area(non_cann_start, non_cann_size, AREA_UNUSABLE);
  intvl_tree_insert(vm->tree, intvl(LOW_HALF_END + 1, HIGH_HALF_START), non_cann_area);

  // recursively mapped region
  uintptr_t recurs_start = get_virt_addr(R_ENTRY, 0, 0, 0);
  uintptr_t recurs_end = get_virt_addr(R_ENTRY, K_ENTRY, K_ENTRY, K_ENTRY);
  size_t recurs_size = recurs_end - recurs_start;
  vm_area_t *recurse_area = alloc_area(recurs_start, recurs_size, AREA_UNUSABLE);
  intvl_tree_insert(vm->tree, intvl(recurs_start, recurs_end), recurse_area);

  // temporary page space
  interval_t temp = intvl(TEMP_PAGE, HIGH_HALF_END);
  size_t temp_size = HIGH_HALF_END - TEMP_PAGE;
  vm_area_t *temp_area = alloc_area(TEMP_PAGE, temp_size, AREA_UNUSABLE);
  intvl_tree_insert(vm->tree, temp, temp_area);

  // kernel space
  uintptr_t kernel_start = KERNEL_VA;
  uintptr_t kernel_end = KERNEL_VA + KERNEL_RESERVED;
  size_t kernel_size = kernel_end - kernel_start;
  vm_area_t *kernel_area = alloc_area(kernel_start, kernel_size, AREA_UNUSABLE | AREA_PHYS);
  kernel_area->phys = boot_info->kernel_phys;
  intvl_tree_insert(vm->tree, intvl(kernel_start, kernel_end), kernel_area);

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

  // finally clear the uefi or bsp created identity mappings
  pml4[0] = 0;
  tlb_flush();

  kprintf("[vm] done!\n");
}

/**
 * Duplicates the current memory space.
 */
vm_t *vm_duplicate() {
  uint64_t *pml4 = get_table((uintptr_t) VM->pml4, 4);
  uint64_t *copy = copy_table(pml4, 4, -1);
  uintptr_t copy_frame = get_frame((uintptr_t) copy);
  copy[R_ENTRY] = copy_frame | PE_WRITE | PE_PRESENT;

  vm_t *vm = kmalloc(sizeof(vm_t));
  vm->tree = copy_intvl_tree(VM->tree);
  vm->pml4 = copy;
  vm->temp_dir = VM->temp_dir;

  return vm;
}

/**
 * Creates kernel page tables for an AP.
 */
void *vm_create_ap_tables() {
  // since these tables are also used when switching
  // from protected to long mode, the tables need to
  // be within the first 4GB of ram.
  page_t *ap_pml4_page = alloc_frame(PE_WRITE | PE_ASSERT);
  page_t *low_pdpt_page = alloc_frame(PE_WRITE | PE_ASSERT);
  page_t *high_pdpt_page = alloc_frame(PE_WRITE | PE_ASSERT);
  page_t *temp_pdt_page = alloc_frame(PE_WRITE | PE_ASSERT);
  page_t *temp_pt_page = alloc_frame(PE_WRITE | PE_ASSERT);

  uint64_t *ap_pml4 = vm_map_page(ap_pml4_page);
  uint64_t *low_pdpt = vm_map_page(low_pdpt_page);
  uint64_t *high_pdpt = vm_map_page(high_pdpt_page);
  uint64_t *high_pdt = vm_map_page(temp_pdt_page);
  uint64_t *high_pt = vm_map_page(temp_pt_page);

  memset(ap_pml4, 0, PAGE_SIZE);
  memset(low_pdpt, 0, PAGE_SIZE);
  memset(high_pdpt, 0, PAGE_SIZE);
  memset(high_pdt, 0, PAGE_SIZE);
  memset(high_pt, 0, PAGE_SIZE);

  ap_pml4[0] = (((uintptr_t) low_pdpt_page->frame) | PE_WRITE | PE_PRESENT);

  // copy over higher half kernel mappings
  uint64_t *kernel_pdpt = (void *) get_virt_addr(R_ENTRY, R_ENTRY, R_ENTRY, K_ENTRY);
  high_pdpt[PDPT_INDEX(KERNEL_VA)] = kernel_pdpt[PDPT_INDEX(KERNEL_VA)];
  high_pdpt[PDPT_INDEX(STACK_VA - 1)] = kernel_pdpt[PDPT_INDEX(STACK_VA - 1)];
  // use new temp dir and temp pt
  high_pdpt[PDPT_INDEX(TEMP_PAGE)] = temp_pdt_page->frame | PE_WRITE | PE_PRESENT;
  high_pdt[PDT_INDEX(TEMP_PAGE)] = temp_pt_page->frame | PE_WRITE | PE_PRESENT;

  // recursive pml4
  ap_pml4[R_ENTRY] = ap_pml4_page->frame | PE_WRITE | PE_PRESENT;
  // high mappings
  ap_pml4[K_ENTRY] = high_pdpt_page->frame | PE_WRITE | PE_PRESENT;
  // first 1gb identity mapped
  low_pdpt[0] = (0 | PE_SIZE | PE_WRITE | PE_PRESENT);
  return (void *) ap_pml4_page->frame;
}

__used void vm_swap_vmspace(vm_t *new_vm) {
  if (VM->pml4 == new_vm->pml4) {
    return;
  }

  uintptr_t frame = get_frame((uintptr_t) new_vm->pml4);
  write_cr3(frame);
  VM = new_vm;
}

//

/**
 * Maps a physical page or pages to an available virtual address.
 */
void *vm_map_page(page_t *page) {
  size_t len = 0;
  page_t *curr = page;
  while (curr) {
    len += page_to_size(curr);
    curr = curr->next;
  }

  uintptr_t address = 0;
  bool success = vm_find_free_area(ABOVE, &address, len);
  if (!success) {
    panic("[vm] no free address space");
  }

  vm_area_t *area = alloc_area(address, len, AREA_USED | AREA_PAGE);
  area->pages = page;
  return map_area(area, 0);
}

/**
 * Maps a physical page to the specified virtual address.
 */
void *vm_map_page_vaddr(uintptr_t virt_addr, page_t *page) {
  uintptr_t address = virt_addr;
  size_t len = 0;
  page_t * curr = page;
  while (curr) {
    len += page_to_size(curr);
    curr = curr->next;
  }

  vm_area_t *area = alloc_area(address, len, AREA_USED | AREA_PAGE);
  area->pages = page;
  return map_area(area, 0);
}

/**
 * Maps a physical page to an address dictated by the given
 * search type and virtual address.
 */
void *vm_map_page_search(page_t *page, vm_search_t search_type, uintptr_t vaddr) {
  size_t len = 0;
  page_t * curr = page;
  while (curr) {
    len += page_to_size(curr);
    curr = curr->next;
  }

  uintptr_t address = vaddr;
  bool success = vm_find_free_area(search_type, &address, len);
  if (!success) {
    panic("[vm] no free address space");
  }
  // kprintf("[vm] address: %p\n", address);

  vm_area_t *area = alloc_area(address, len, AREA_USED | AREA_PAGE);
  area->pages = page;
  return map_area(area, 0);
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

  vm_area_t *area = alloc_area(address, len, AREA_USED | AREA_PHYS);
  area->phys = phys_addr;
  flags |= PE_PRESENT;
  return map_area(area, flags);
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

  vm_area_t *area = alloc_area(virt_addr, len, AREA_USED | AREA_PHYS);
  area->phys = phys_addr;
  flags |= PE_PRESENT;
  return map_area(area, flags);
}

//

/**
 * Reserves a part of the address space with the given size.
 * The reserved area can only be mapped by calling vm_map_vaddr
 * with the same size as was reserved. The reserved area will not
 * be automatically used by the other vm functions.
 */
uintptr_t vm_reserve(size_t len) {
  len = align(len, PAGE_SIZE);
  uintptr_t address = 0;
  bool success = vm_find_free_area(ABOVE, &address, len);
  if (!success) {
    panic("[vm] no free address space");
  }

  vm_area_t *area = alloc_area(address, len, AREA_RESERVED);
  intvl_tree_insert(VM->tree, intvl(area->base, area->base + area->size), area);
  return address;
}

/**
 * Marks the virtual area given by the address and length as reserved.
 * The reserved area can only be mapped by calling vm_map_vaddr
 * with the same size as was reserved. The reserved area will not
 * be automatically used by the other vm functions.
 */
void vm_mark_reserved(uintptr_t virt_addr, size_t len) {
  len = align(len, PAGE_SIZE);
  interval_t interval = intvl(virt_addr, virt_addr + len);
  intvl_node_t *existing = intvl_tree_find(VM->tree, interval);
  if (existing) {
    panic("[vm] failed to reserve address - already in use\n");
  }

  vm_area_t *area = alloc_area(virt_addr, len, AREA_RESERVED);
  intvl_tree_insert(VM->tree, interval, area);
}

//

void vm_update_page(page_t *page, uint16_t flags) {
  update_page_flags(page, flags);
  if (page->entry != NULL) {
    *page->entry &= PAGE_FRAME_MASK;
    *page->entry &= page_to_flags(page);
  }
}

void vm_update_pages(page_t *page, uint16_t flags) {
  while (page) {
    vm_update_page(page, flags);
    page = page->next;
  }
  tlb_flush();
}

//

/**
 * Unmaps a mapped physical page or pages.
 */
void vm_unmap_page(page_t *page) {
  if (!page->flags.present) {
    return;
  }

  interval_t intvl = intvl(page->addr, page->addr + 1);
  intvl_node_t *intvl_node = intvl_tree_find(VM->tree, intvl);
  if (intvl_node == NULL) {
    panic("[vm] page is not mapped");
  }

  vm_area_t *area = intvl_node->data;
  page_t *curr = page;
  while (curr) {
    *curr->entry &= ~PE_PRESENT;
    page->flags.present = 0;
    curr = curr->next;
  }

  intvl_tree_delete(VM->tree, intvl);
  kfree(area);

  tlb_flush();
}

/**
 * Unamps a mapped virtual address.
 */
void vm_unmap_vaddr(uintptr_t virt_addr) {
  interval_t intvl = intvl(virt_addr, virt_addr + 1);
  intvl_node_t *intvl_node = intvl_tree_find(VM->tree, intvl);
  if (intvl_node == NULL) {
    panic("[vm] address is not mapped");
  }

  vm_area_t *area = intvl_node->data;
  map_page(virt_addr, 0, 0);
  intvl_tree_delete(VM->tree, intvl);
  kfree(area);
}


//

/**
 * Returns the page struct for the given virtual address or
 * `NULL` if one does not exist.
 */
page_t *vm_get_page(uintptr_t addr) {
  vm_area_t *area = vm_get_vm_area(addr);
  if (area == NULL || area->pages == NULL) {
    return NULL;
  }

  page_t *page = area->pages;
  while (page) {
    if (addr >= page->addr && addr < page->addr + page_to_size(page)) {
      // page contains the address
      return page;
    }

    page = page->next;
  }
  return NULL;
}

/**
 * Returns the vm_area struct associated with the given
 * address, or `NULL` if one does not exist.
 */
vm_area_t *vm_get_vm_area(uintptr_t addr) {
  intvl_node_t *node = intvl_tree_find(VM->tree, intvl(addr, addr + 1));
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

  int alignment = PAGE_SIZE;
  if (size >= PAGE_SIZE_1GB) {
    alignment = PAGE_SIZE_1GB;
  } else if (size >= PAGE_SIZE_2MB) {
    alignment = PAGE_SIZE_2MB;
  }

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

    // if two consequtive nodes are not contiguous in memory
    // check that there is enough space between the them to
    // fit the requested area.
    bool contig = contiguous(i, j);
    if (search_type == ABOVE) {
      if (!contig && i.start > ptr && i.start - ptr >= size) {
        *addr = ptr;
        return true;
      }
      ptr = align(i.end, alignment);
    } else {
      if (!contig && i.end < ptr && ptr - i.end >= size) {
        *addr = ptr;
        return true;
      }
      ptr = align(i.start, alignment) - 1;
    }

    last = node;
  }
  return false;
}

//
// Debugging
//

void vm_print_debug_mappings() {
  intvl_iter_t *iter = intvl_iter_tree(VM->tree);
  intvl_node_t *node;

  kprintf("====== Virtual Mappings ======\n");
  while ((node = intvl_iter_next(iter))) {
    interval_t i = node->interval;
    kprintf("%018p - %018p | %llu\n", i.start, i.end, i.end - i.start);
  }
  kprintf("==============================\n");
  kfree(iter);
}
