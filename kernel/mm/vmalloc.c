//
// Created by Aaron Gill-Braun on 2022-06-17.
//

#include <mm/vmalloc.h>
#include <mm/pmalloc.h>
#include <mm/pgtable.h>
#include <mm/heap.h>
#include <mm/init.h>

#include <cpu/cpu.h>
#include <debug/debug.h>

#include <irq.h>
#include <panic.h>
#include <string.h>
#include <printf.h>

#include <interval_tree.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(x, ...) kprintf(x, ##__VA_ARGS__)
#define PANIC_IF(x, msg, ...) { if (x) panic(msg, ##__VA_ARGS__); }

// these are the default hints for different combinations of vm flags
// they are used as a starting point for the kernel when searching for
// a free region
#define HINT_USER_DEFAULT   0x0000000040000000ULL // for VM_USER
#define HINT_USER_MALLOC    0x0000010000000000ULL // for VM_USER|VM_MALLOC
#define HINT_USER_STACK     0x00007FFFFFFFFFFFULL // for VM_USER|VM_STACK
#define HINT_KERNEL_DEFAULT 0xFFFFC00000000000ULL // for no flags
#define HINT_KERNEL_MALLOC  0xFFFFC01000000000ULL // for VM_MALLOC
#define HINT_KERNEL_STACK   0xFFFFFF8040000000ULL // for VM_STACK

// internal vm flags
#define VM_MAPPED 0x1000 // mapping is currently active
#define VM_MALLOC 0x2000 // mapping is a vmalloc allocation
#define VM_CONTIG 0x4000 // mapping is physically contiguous


void execute_init_address_space_callbacks();
extern uintptr_t entry_initial_stack_top;
address_space_t *kernel_space;

// called from thread.asm
__used void swap_address_space(address_space_t *new_space) {
  address_space_t *current = PERCPU_ADDRESS_SPACE;
  if (current != NULL && current->page_table == new_space->page_table) {
    return;
  }
  set_current_pgtable(new_space->page_table);
  PERCPU_SET_ADDRESS_SPACE(new_space);
}


static always_inline bool space_contains(address_space_t *space, uintptr_t addr) {
  return addr >= space->min_addr && addr < space->max_addr;
}

static always_inline interval_t mapping_interval(vm_mapping_t *vm) {
  // if the mapping is a stack mapping and it has a guard the interval base
  // address is one page below the vm address to account for the guard page
  if (vm->flags & VM_STACK && vm->flags & VM_GUARD)
    return intvl(vm->address - PAGE_SIZE, vm->address + vm->virt_size - PAGE_SIZE);

  return intvl(vm->address, vm->address + vm->virt_size);
}

static always_inline size_t empty_space_size(vm_mapping_t *vm) {
  size_t size = vm->virt_size - vm->size;
  if (vm->flags & VM_STACK)
    size -= PAGE_SIZE;
  return size;
}

static inline uintptr_t choose_best_hint(address_space_t *space, uintptr_t hint, uint32_t vm_flags) {
  if (hint != 0) {
    if (space_contains(space, hint)) {
      // caller has provided a hint, use it
      return hint;
    }
    kprintf("vmalloc: hint %p is not in target address space\n", hint);
  }

  if (vm_flags & VM_USER) {
    if (vm_flags & VM_MALLOC)
      return HINT_USER_MALLOC;
    if (vm_flags & VM_STACK)
      return HINT_USER_STACK;
    return HINT_USER_DEFAULT;
  } else {
    if (vm_flags & VM_MALLOC)
      return HINT_KERNEL_MALLOC;
    if (vm_flags & VM_STACK)
      return HINT_KERNEL_STACK;
    return HINT_KERNEL_DEFAULT;
  }
}

//
// mapping/unmapping of the various vm_types
//

static void vm_map_phys_internal(vm_mapping_t *vm, uint32_t pg_flags) {
  ASSERT(vm->type == VM_TYPE_PHYS);
  address_space_t *space = vm->space;
  size_t stride = pg_flags_to_size(pg_flags);
  size_t count = vm->size / stride;

  uintptr_t ptr = vm->address;
  uintptr_t phys_ptr = vm->vm_phys;
  while (count > 0) {
    page_t *table_pages = NULL;
    recursive_map_entry(ptr, phys_ptr, pg_flags, &table_pages);
    ptr += stride;
    phys_ptr += stride;
    count--;

    if (table_pages != NULL) {
      page_t *last_page = SLIST_GET_LAST(table_pages, next);
      SLIST_ADD_SLIST(&space->table_pages, table_pages, last_page, next);
    }
  }

  cpu_flush_tlb();
}

static void vm_unmap_phys_internal(vm_mapping_t *vm) {
  ASSERT(vm->type == VM_TYPE_PHYS);
  uint32_t pg_flags = vm->pg_flags;
  size_t stride = pg_flags_to_size(pg_flags);
  size_t count = vm->size / stride;

  uintptr_t ptr = vm->address;
  while (count > 0) {
    recursive_unmap_entry(ptr, pg_flags);
    ptr += stride;
    count--;
  }

  cpu_flush_tlb();
}

static void vm_resize_phys_internal(vm_mapping_t *vm, uintptr_t oldaddr, size_t oldsize, size_t newsize) {
  ASSERT(vm->type == VM_TYPE_PHYS);
  uint32_t pg_flags = vm->pg_flags;
  size_t stride = pg_flags_to_size(pg_flags);
  size_t oldcount = oldsize / stride;
  size_t newcount = newsize / stride;
  ASSERT(newsize % stride == 0); // newsize must be page aligned

  if (newcount < oldcount) {
    uintptr_t ptr = oldaddr + newsize;
    while (newcount < oldcount) {
      recursive_unmap_entry(ptr, pg_flags);
      ptr += stride;
      oldcount--;
    }
  } else if (newcount > oldcount) {
    uintptr_t ptr = vm->address + oldsize;
    uintptr_t phys_ptr = vm->vm_phys + oldsize;
    while (newcount > oldcount) {
      page_t *table_pages = NULL;
      recursive_map_entry(ptr, phys_ptr, pg_flags, &table_pages);
      ptr += stride;
      phys_ptr += stride;
      oldcount++;

      if (table_pages != NULL) {
        page_t *last_page = SLIST_GET_LAST(table_pages, next);
        SLIST_ADD_SLIST(&vm->space->table_pages, table_pages, last_page, next);
      }
    }

  }

  cpu_flush_tlb();
}

static void vm_map_pages_internal(vm_mapping_t *vm, size_t off, page_t *pages, uint32_t pg_flags) {
  ASSERT(vm->type == VM_TYPE_PAGE);
  ASSERT(off < vm->size);
  address_space_t *space = vm->space;

  uintptr_t ptr = vm->address + off;
  page_t *curr = pages;
  while (curr != NULL) {
    page_t *table_pages = NULL;
    recursive_map_entry(ptr, curr->address, curr->flags, &table_pages);

    size_t sz = pg_flags_to_size(curr->flags);
    ptr += sz;
    curr->mapping = vm;
    curr = curr->next;

    if (table_pages != NULL) {
      page_t *last_page = SLIST_GET_LAST(table_pages, next);
      SLIST_ADD_SLIST(&space->table_pages, table_pages, last_page, next);
    }
  }

  cpu_flush_tlb();
}

static void vm_unmap_pages_internal(vm_mapping_t *vm, size_t off, size_t size) {
  ASSERT(vm->type == VM_TYPE_PAGE);
  ASSERT(off + size < vm->size);

  uintptr_t ptr = vm->address;
  page_t *curr = vm->vm_pages;
  while (off > 0) {
    // get to page at offset
    ptr += pg_flags_to_size(curr->flags);
    curr = curr->next;
    off--;
  }

  uintptr_t max_ptr = ptr + size;
  while (ptr < max_ptr && curr != NULL) {
    recursive_unmap_entry(ptr, curr->flags);
    ptr += pg_flags_to_size(curr->flags);
    curr->mapping = NULL;
    // dont free the pages until the mapping is destroyed
    curr = curr->next;
  }

  cpu_flush_tlb();
}

static void vm_resize_pages_internal(vm_mapping_t *vm, uintptr_t oldaddr, size_t oldsize, size_t newsize) {
  ASSERT(vm->type == VM_TYPE_PAGE);

  if (newsize < oldsize) {
    // unmap the pages that are not used anymore
    vm_unmap_pages_internal(vm, newsize, oldsize - newsize);
  } else if (newsize > oldsize) {
    // leave the new pages unmapped, they will be mapped later
  }
}

static void vm_map_file_internal(vm_mapping_t *vm, size_t off, page_t *pages, uint32_t pg_flags) {
  ASSERT(vm->type == VM_TYPE_FILE);
  ASSERT(off < vm->size);
  struct vm_file *file = vm->vm_file;
  address_space_t *space = vm->space;

  size_t stride = pg_flags_to_size(pg_flags);
  if (file->pages == NULL) {
    size_t num_pages = vm->size / stride;
    size_t arrsz = num_pages * sizeof(page_t *);

    // first time, allocate array to hold mapped pages
    if (arrsz >= PAGE_SIZE) {
      file->pages = vmalloc(arrsz, PG_WRITE);
    } else {
      file->pages = kmalloc(arrsz);
    }
    memset(file->pages, 0, arrsz);
  }

  if (pages == NULL) {
    // no pages to map, just return
    return;
  }

  uintptr_t ptr = vm->address + off;
  size_t index = off / stride;
  while (pages != NULL) {
    if (file->pages[index] != NULL) {
      panic("vm_map_file_internal: page already mapped at offset %d [vm={:str}]\n", index * stride, &vm->name);
    }

    // separate page from the list
    page_t *curr = pages;
    pages = pages->next;
    curr->next = NULL;

    page_t *table_pages = NULL;
    recursive_map_entry(ptr, curr->address, curr->flags, &table_pages);

    file->pages[index] = curr;
    curr->mapping = vm;
    ptr += stride;
    file->mapped_size += stride;
    index++;

    if (table_pages != NULL) {
      page_t *last_page = SLIST_GET_LAST(table_pages, next);
      SLIST_ADD_SLIST(&space->table_pages, table_pages, last_page, next);
    }
  }

  cpu_flush_tlb();
}

static void vm_unmap_file_internal(vm_mapping_t *vm, size_t off, size_t size) {
  ASSERT(vm->type == VM_TYPE_FILE);
  ASSERT(off + size < vm->size);
  struct vm_file *file = vm->vm_file;
  size_t stride = pg_flags_to_size(vm->pg_flags);

  uintptr_t ptr = vm->address;
  size_t start_index = off / stride;
  size_t max_index = (off + size) / stride;
  for (size_t i = start_index; i < max_index; i++) {
    page_t *page = file->pages[i];
    if (page != NULL) {
      recursive_unmap_entry(ptr, page->flags);
      page->mapping = NULL;
      file->pages[i] = NULL;
      free_pages(page);

      file->mapped_size -= stride;
    }
    ptr += stride;
  }

  cpu_flush_tlb();
}

static void vm_resize_file_internal(vm_mapping_t *vm, uintptr_t oldaddr, size_t oldsize, size_t newsize) {
  ASSERT(vm->type == VM_TYPE_FILE);
  size_t stride = pg_flags_to_size(vm->pg_flags);

  if (newsize < oldsize) {
    // unmap the pages that outside of the new size
    vm_unmap_file_internal(vm, newsize, oldsize - newsize);
  } else if (newsize > oldsize) {
    // leave the new pages unmapped, they will be filled on demand
  }
}

//

static uintptr_t get_free_region(address_space_t *space, uintptr_t base, size_t size, uintptr_t align,
                                 uint32_t vm_flags, vm_mapping_t **closest_vm) {
  uintptr_t addr = base;
  interval_t interval = intvl(base, base + size);
  intvl_node_t *closest = intvl_tree_find_closest(space->new_tree, interval);
  if (!closest)
    return addr; // first mapping
  if (!overlaps(interval, closest->interval)) {
    *closest_vm = closest->data; // the given base address is free
    return addr;
  }

  vm_mapping_t *curr = closest->data;
  vm_mapping_t *prev = NULL;
  while (curr != NULL) {
    interval_t i = mapping_interval(curr);
    interval_t j = prev ? mapping_interval(prev) : i;

    // if two consequtive nodes are not contiguous in memory
    // check that there is enough space between the them to
    // fit the requested area.

    if (vm_flags & VM_STACK) {
      // go backwards looking for a free space from the top of each free region
      bool contig = contiguous(j, i);
      if (!contig && j.start >= addr && j.start - addr >= size)
        break;

      if (i.start < size)
        return 0; // no space

      addr = align(i.start - size, align);
      prev = curr;
      curr = LIST_PREV(curr, list);
    } else {
      // go forward looking for a free space from the bottom of each free region
      bool contig = contiguous(i, j);
      if (!contig && i.start > addr && i.start - addr >= size)
        break;

      addr = align(i.end, align);
      prev = curr;
      curr = LIST_NEXT(curr, list);
    }
  }

  if (size > (UINT64_MAX - addr) || addr + size > space->max_addr) {
    panic("no free address space");
  }

  *closest_vm = prev;
  return addr;
}

static bool check_range_free(address_space_t *space, uintptr_t base, size_t size, uint32_t vm_flags, vm_mapping_t **prev_vm) {
  interval_t interval = intvl(base, base + size);
  intvl_node_t *closest = intvl_tree_find_closest(space->new_tree, interval);
  if (closest == NULL) {
    return true;
  }

  if (!overlaps(interval, closest->interval)) {
    *prev_vm = closest->data;
    return true;
  }
  return false;
}

static bool resize_mapping_inplace(vm_mapping_t *vm, size_t new_size) {
  // vm should be locked while calling this
  address_space_t *space = vm->space;
  interval_t interval = mapping_interval(vm);
  intvl_node_t *node = intvl_tree_find(space->new_tree, interval);
  ASSERT(node && node->data == vm);

  // if we are shrinking or growing within the existing empty node virtual space
  // we dont need to update the tree just the mapping size and address
  size_t delta = new_size - vm->size;
  if (new_size < vm->size) {
    vm->size = new_size;
    if (vm->flags & VM_STACK)
      vm->address += delta;
    return true;
  } else if (new_size > vm->size && new_size <= empty_space_size(vm)) {
    vm->size = new_size;
    if (vm->flags & VM_STACK)
      vm->address -= delta;
    return true;
  }

  // for growing beyond the virtual space of the node we need to update the tree
  // but first we need to make sure we dont overlap with the next node
  SPIN_LOCK(&space->lock);
  if (vm->flags & VM_STACK) {
    vm_mapping_t *prev = LIST_PREV(vm, list);
    intvl_node_t *prev_node = intvl_tree_find(space->new_tree, mapping_interval(prev));

    // |--prev--| empty space |---vm---|
    size_t empty_space = interval.start - prev_node->interval.end + empty_space_size(vm);
    if (empty_space < delta) {
      SPIN_UNLOCK(&space->lock);
      return false;
    }

    node->interval.start -= delta;
    vm->address -= new_size - vm->size;
    vm->size = new_size;
  } else {
    vm_mapping_t *next = LIST_NEXT(vm, list);
    intvl_node_t *next_node = intvl_tree_find(space->new_tree, mapping_interval(next));

    // |---vm---| empty space |--next--|
    size_t empty_space = next_node->interval.start - interval.end + empty_space_size(vm);
    if (empty_space < delta) {
      SPIN_UNLOCK(&space->lock);
      return false;
    }

    node->interval.end += delta;
    vm->size = new_size;
  }

  return true;
}

static bool move_mapping(vm_mapping_t *vm, size_t newsize) {
  // space should be locked while calling this
  address_space_t *space = vm->space;
  uintptr_t base = vm->address;
  size_t virt_size = newsize;

  size_t off = 0;
  if (vm->flags & VM_GUARD) {
    virt_size += PAGE_SIZE;
    off = PAGE_SIZE;
  }

  if (vm->flags & VM_STACK) {
    base -= virt_size;
  }

  // look for a new free region
  vm_mapping_t *closest = NULL;
  uintptr_t virt_addr = get_free_region(space, base, virt_size, pg_flags_to_size(vm->pg_flags), vm->flags, &closest);
  if (virt_addr == 0) {
    return false;
  }

  // remove from the old node tree and insert the new one
  intvl_tree_delete(space->new_tree, mapping_interval(vm));
  intvl_tree_insert(space->new_tree, intvl(virt_addr, virt_addr + virt_size), vm);

  // switch place of the mapping in the space list
  LIST_REMOVE(&space->mappings, vm, list);
  if (closest->address > virt_addr) {
    closest = LIST_PREV(closest, list);
  }
  // insert into the list
  LIST_INSERT(&space->mappings, vm, list, closest);

  // update the mapping
  vm->address = virt_addr + off;
  vm->size = newsize;
  vm->virt_size = virt_size;
  return true;
}

//

void page_fault_handler(uint8_t vector, uint32_t error_code, cpu_irq_stack_t *frame, cpu_registers_t *regs) {
  per_cpu_t *percpu = __percpu_struct_ptr();
  uint32_t id = PERCPU_ID;
  uint64_t fault_addr = __read_cr2();
  if (fault_addr == 0)
    goto exception;


  if (!(error_code & CPU_PF_P)) {
    // fault was due to a non-present page this might be recoverable
    // check if this fault is related to a vm mapping
    vm_mapping_t *vm = vm_get_mapping(fault_addr);
    if (vm == NULL || vm->type != VM_TYPE_FILE) {
      // TODO: support extending stacks automatically if the fault happens
      //       in the guard page
      goto exception;
    }

    DPRINTF("non-present page fault in vm_file [vm={:str},addr=%p]\n", &vm->name, fault_addr);
    struct vm_file *file = vm->vm_file;
    size_t off = fault_addr - vm->address;
    page_t *page = file->get_page(vm, off, vm->pg_flags);
    if (!page) {
      DPRINTF("failed to get non-present page in vm_file [vm={:str},off=%zu]\n", &vm->name, off);
      goto exception;
    }

    // map the new page into the file
    vm_map_file_internal(vm, off, page, vm->pg_flags);
    return; // recover
  }

  // TODO: support COW pages on CPU_PF_W

LABEL(exception);
  kprintf("================== !!! Exception !!! ==================\n");
  kprintf("  Page Fault  - Data: %#b\n", error_code);
  kprintf("  CPU#%d  -  RIP: %p  -  CR2: %018p\n", id, frame->rip, fault_addr);

  uintptr_t rip = frame->rip - 8;
  uintptr_t rbp = regs->rbp;

  char *line_str = debug_addr2line(rip);
  kprintf("  %s\n", line_str);
  kfree(line_str);

  debug_unwind(rip, rbp);
  while (true) {
    cpu_pause();
  }
}

//

void init_address_space() {
  kernel_space = kmallocz(sizeof(address_space_t));
  kernel_space->tree = create_intvl_tree();
  kernel_space->new_tree = create_intvl_tree();
  kernel_space->min_addr = KERNEL_SPACE_START;
  kernel_space->max_addr = KERNEL_SPACE_END;
  LIST_INIT(&kernel_space->table_pages);
  spin_init(&kernel_space->lock);

  address_space_t *user_space = kmallocz(sizeof(address_space_t));
  user_space->tree = create_intvl_tree();
  user_space->new_tree = create_intvl_tree();
  user_space->min_addr = USER_SPACE_START;
  user_space->max_addr = USER_SPACE_END;
  LIST_INIT(&user_space->table_pages);
  spin_init(&user_space->lock);
  PERCPU_SET_ADDRESS_SPACE(user_space);

  uintptr_t pgtable = get_current_pgtable();
  init_recursive_pgtable((void *) pgtable, pgtable);
  kernel_space->page_table = pgtable;
  user_space->page_table = pgtable;

  // set up the starting address space layout
  size_t lowmem_size = kernel_address;
  size_t kernel_code_size = kernel_code_end - kernel_code_start;
  size_t kernel_data_size = kernel_data_end - kernel_code_end;
  size_t reserved_size = kernel_reserved_va_ptr - KERNEL_RESERVED_VA;

  vm_alloc_rsvd(0, PAGE_SIZE, VM_FIXED | VM_USER, "null");
  vm_alloc_phys(0, kernel_virtual_offset, lowmem_size, VM_FIXED, "reserved");
  vm_alloc_phys(kernel_address, kernel_code_start, kernel_code_size, VM_FIXED, "kernel code");
  vm_alloc_phys(kernel_address+kernel_code_size, kernel_code_end, kernel_data_size, VM_FIXED, "kernel data");
  vm_alloc_phys(kheap_phys_addr(), KERNEL_HEAP_VA, KERNEL_HEAP_SIZE, VM_FIXED, "kernel heap");
  vm_alloc_phys(kernel_reserved_start, KERNEL_RESERVED_VA, reserved_size, VM_FIXED, "kernel reserved");

  page_t *stack_pages = alloc_pages(SIZE_TO_PAGES(KERNEL_STACK_SIZE), PG_WRITE);
  vm_mapping_t *stack_vm = vm_alloc_pages(stack_pages, 0, KERNEL_STACK_SIZE, VM_STACK | VM_GUARD, "kernel stack");
  vm_map(stack_vm, PG_WRITE);

  execute_init_address_space_callbacks();

  // relocate boot info struct
  static_assert(sizeof(boot_info_v2_t) <= PAGE_SIZE);
  boot_info_v2 = vm_alloc_map_phys((uintptr_t) boot_info_v2, 0, PAGE_SIZE, 0, 0, "boot info");

  vm_print_address_space();

  // switch to new kernel stack
  kprintf("switching to new kernel stack\n");
  uint64_t rsp = cpu_read_stack_pointer();
  uint64_t stack_offset = ((uint64_t) &entry_initial_stack_top) - rsp;

  uint64_t new_rsp = stack_vm->address + stack_vm->size - stack_offset;
  memcpy((void *) new_rsp, (void *) rsp, stack_offset);
  cpu_write_stack_pointer(new_rsp);

  irq_register_exception_handler(CPU_EXCEPTION_PF, page_fault_handler);
  pgtable_unmap_user_mappings();
}

void init_ap_address_space() {
  address_space_t *user_space = kmalloc(sizeof(address_space_t));
  user_space->tree = create_intvl_tree();
  user_space->new_tree = create_intvl_tree();
  user_space->min_addr = USER_SPACE_START;
  user_space->max_addr = USER_SPACE_END;
  user_space->page_table = get_current_pgtable();
  spin_init(&user_space->lock);
  LIST_INIT(&user_space->table_pages);
  PERCPU_SET_ADDRESS_SPACE(user_space);

  vm_alloc_rsvd(0, PAGE_SIZE, VM_FIXED | VM_USER, "null");
}

uintptr_t make_ap_page_tables() {
  page_t *pml4_pages = NULL;
  uintptr_t pml4 = create_new_ap_page_tables(&pml4_pages);
  return pml4;
}

// TODO: make sure this works
address_space_t *fork_address_space() {
  address_space_t *current = PERCPU_ADDRESS_SPACE;
  address_space_t *space = kmalloc(sizeof(address_space_t));
  space->tree = copy_intvl_tree(current->tree);
  space->min_addr = current->min_addr;
  space->max_addr = current->max_addr;
  LIST_INIT(&space->table_pages);
  spin_init(&space->lock);

  // fork page tables
  page_t *meta_pages = NULL;
  uintptr_t pgtable = deepcopy_fork_page_tables(&meta_pages);
  space->page_table = pgtable;
  SLIST_ADD_SLIST(&space->table_pages, meta_pages, SLIST_GET_LAST(meta_pages, next), next);

  return space;
}

//
//

vm_mapping_t *vm_alloc(enum vm_type type, uintptr_t hint, size_t size, uint32_t vm_flags, const char *name) {
  vm_mapping_t *vm = kmallocz(sizeof(vm_mapping_t));
  vm->type = type;
  vm->flags = vm_flags;
  vm->virt_size = size;
  vm->size = size;
  spin_init(&vm->lock);

  address_space_t *space;
  if (vm_flags & VM_USER) {
    space = PERCPU_ADDRESS_SPACE;
  } else {
    space = kernel_space;
  }

  size_t off = 0;
  if (vm_flags & VM_GUARD) {
    vm->virt_size += PAGE_SIZE;
    if (vm_flags & VM_STACK) {
      off = PAGE_SIZE;
    }
  }

  SPIN_LOCK(&space->lock);
  uintptr_t virt_addr = 0;
  vm_mapping_t *closest = NULL;
  if (vm_flags & VM_FIXED) {
    if (!space_contains(space, hint)) {
      panic("vm_alloc: hint address not in address space: %p\n", hint);
    }

    if (vm_flags & VM_STACK) {
      if (hint < vm->virt_size) {
        SPIN_UNLOCK(&space->lock);
        kfree(vm);
        panic("vm_alloc: hint address is too low for requested stack size");
      }
      hint -= vm->virt_size;
    }
    virt_addr = hint;

    // make sure the requested range is free
    if (!check_range_free(space, hint, vm->virt_size, vm_flags, &closest)) {
      SPIN_UNLOCK(&space->lock);
      kfree(vm);
      kprintf("vm_alloc: requested fixed address range is not free\n");
      return NULL;
    }
  } else {
    // dynamically allocated
    uintptr_t align = PAGE_SIZE;
    if (size >= SIZE_2MB) {
      align = SIZE_2MB; // in case big pages are used
    } else if (size >= SIZE_1GB) {
      align = SIZE_1GB; // in case huge pages are used
    }

    hint = choose_best_hint(space, hint, vm_flags);
    if (vm_flags & VM_STACK) {
      ASSERT(hint > vm->virt_size);
      hint -= vm->virt_size;
    }

    virt_addr = get_free_region(space, hint, vm->virt_size, align, vm_flags, &closest);
    if (virt_addr == 0) {
      SPIN_UNLOCK(&space->lock);
      kfree(vm);
      kprintf("vm_alloc: failed to satisfy allocation request\n");
      return NULL;
    }
  }

  vm->address = virt_addr + off;
  vm->name = str_new(name);
  vm->space = space;

  // add it to the mappings list
  if (closest) {
    if (closest->address > virt_addr) {
      // we dont care about closeness here we just want the mapping
      // immediately before where the new mapping is going to be
      closest = LIST_PREV(closest, list);
    }

    // insert into the list
    LIST_INSERT(&space->mappings, vm, list, closest);
  } else {
    // first mapping
    LIST_ADD(&space->mappings, vm, list);
  }

  // insert into address space
  intvl_tree_insert(space->new_tree, mapping_interval(vm), vm);
  space->num_mappings++;
  SPIN_UNLOCK(&space->lock);
  return vm;
}

vm_mapping_t *vm_alloc_rsvd(uintptr_t hint, size_t size, uint32_t vm_flags, const char *name) {
  return vm_alloc(VM_TYPE_RSVD, hint, size, vm_flags, name);
}

vm_mapping_t *vm_alloc_phys(uintptr_t phys_addr, uintptr_t hint, size_t size, uint32_t vm_flags, const char *name) {
  vm_mapping_t *vm = vm_alloc(VM_TYPE_PHYS, hint, size, vm_flags, name);
  if (!vm)
    return NULL;

  vm->vm_phys = phys_addr;
  return vm;
}

vm_mapping_t *vm_alloc_pages(page_t *pages, uintptr_t hint, size_t size, uint32_t vm_flags, const char *name) {
  vm_mapping_t *vm = vm_alloc(VM_TYPE_PAGE, hint, size, vm_flags, name);
  if (!vm)
    return NULL;

  vm->vm_pages = pages;
  return vm;
}

vm_mapping_t *vm_alloc_file(vm_getpage_t get_page_fn, uintptr_t hint, size_t size, uint32_t vm_flags, const char *name) {
  vm_mapping_t *vm = vm_alloc(VM_TYPE_FILE, hint, size, vm_flags | VM_GROWS, name);
  if (!vm)
    return NULL;

  struct vm_file *file = kmallocz(sizeof(struct vm_file));
  file->get_page = get_page_fn;
  file->full_size = size;
  vm->vm_file = file;
  return vm;
}

void *vm_alloc_map_phys(uintptr_t phys_addr, uintptr_t hint, size_t size, uint32_t vm_flags, uint32_t pg_flags, const char *name) {
  vm_mapping_t *vm = vm_alloc_phys(phys_addr, hint, size, vm_flags, name);
  PANIC_IF(!vm, "vm_alloc_map_phys: failed to allocate mapping \"%s\"", name);
  void *ptr = vm_map(vm, pg_flags);
  PANIC_IF(!ptr, "vm_alloc_map_phys: failed to map mapping \"%s\"", name);
  return ptr;
}

void *vm_alloc_map_pages(page_t *pages, uintptr_t hint, size_t size, uint32_t vm_flags, uint32_t pg_flags, const char *name) {
  vm_mapping_t *vm = vm_alloc_pages(pages, hint, size, vm_flags, name);
  PANIC_IF(!vm, "vm_alloc_map_pages: failed to allocate mapping \"%s\"", name);
  void *ptr = vm_map(vm, pg_flags);
  PANIC_IF(!ptr, "vm_alloc_map_pages: failed to map mapping \"%s\"", name);
  return ptr;
}

void *vm_alloc_map_file(vm_getpage_t get_page_fn, uintptr_t hint, size_t size, uint32_t vm_flags, uint32_t pg_flags, const char *name) {
  vm_mapping_t *vm = vm_alloc_file(get_page_fn, hint, size, vm_flags, name);
  PANIC_IF(!vm, "vm_alloc_map_file: failed to allocate mapping \"%s\"", name);
  void *ptr = vm_map(vm, pg_flags);
  PANIC_IF(!ptr, "vm_alloc_map_file: failed to map mapping \"%s\"", name);
  return ptr;
}

void vm_free(vm_mapping_t *vm) {
  if (vm->flags & VM_MAPPED) {
    vm_unmap(vm);
  }

  if (vm->type == VM_TYPE_PAGE && vm->vm_pages != NULL) {
    free_pages(vm->vm_pages);
    vm->vm_pages = NULL;
  }

  address_space_t *space = vm->space;
  SPIN_LOCK(&space->lock);
  LIST_REMOVE(&space->mappings, vm, list);
  intvl_tree_delete(space->new_tree, mapping_interval(vm));
  space->num_mappings--;
  SPIN_LOCK(&space->lock);

  str_free(&vm->name);
  memset(vm, 0, sizeof(vm_mapping_t));
  kfree(vm);
}

void *vm_map(vm_mapping_t *vm, uint32_t pg_flags) {
  ASSERT(vm->type != VM_TYPE_RSVD);
  size_t off;

  SPIN_LOCK(&vm->lock);
  ASSERT(!(vm->flags & VM_MAPPED));
  switch (vm->type) {
    case VM_TYPE_PHYS:
      vm_map_phys_internal(vm, pg_flags);
      break;
    case VM_TYPE_PAGE:
      vm_map_pages_internal(vm, 0, vm->vm_pages, pg_flags);
      break;
    case VM_TYPE_FILE:
      vm_map_file_internal(vm, 0, NULL, pg_flags);
      break;
    default:
      unreachable;
  }

  vm->flags |= VM_MAPPED;
  vm->pg_flags = pg_flags;
  SPIN_UNLOCK(&vm->lock);
  return (void *) vm->address;
}

void vm_unmap(vm_mapping_t *vm) {
  SPIN_LOCK(&vm->lock);
  ASSERT(vm->flags & VM_MAPPED);
  switch (vm->type) {
    case VM_TYPE_PHYS:
      vm_unmap_phys_internal(vm);
      break;
    case VM_TYPE_PAGE:
      vm_unmap_pages_internal(vm, 0, vm->size);
      break;
    case VM_TYPE_FILE:
      vm_unmap_file_internal(vm, 0, vm->size);
      break;
    default:
      unreachable;
  }

  vm->flags &= ~VM_MAPPED;
  vm->pg_flags = 0;
  SPIN_UNLOCK(&vm->lock);
}

int vm_resize(vm_mapping_t *vm, size_t new_size, bool allow_move) {
  address_space_t *space = vm->space;
  if (!(vm->flags & VM_GROWS)) {
    panic("vm_resize: mapping \"%s\" cannot be resized", vm->name);
  }

  SPIN_LOCK(&vm->lock);
  if (vm->size == new_size) {
    SPIN_UNLOCK(&vm->lock);
    return 0;
  }

  // first try resizing the existing mapping in place
  uintptr_t old_addr = vm->address;
  size_t old_size = vm->size;
  if (resize_mapping_inplace(vm, new_size)) {
    SPIN_UNLOCK(&vm->lock);
    goto update_memory;
  }

  // okay that didnt work but we can try moving the mapping
  if (!allow_move) {
    SPIN_UNLOCK(&vm->lock);
    return -1;
  }

  SPIN_LOCK(&space->lock);
  bool ok = move_mapping(vm, new_size);
  SPIN_UNLOCK(&space->lock);
  SPIN_UNLOCK(&vm->lock);
  if (!ok) {
    return -1;
  }

  // finally call the appropriate resize function to update the underlying mappings
LABEL(update_memory);
  switch (vm->type) {
    case VM_TYPE_PHYS:
      vm_resize_phys_internal(vm, old_addr, old_size, new_size);
      break;
    case VM_TYPE_PAGE:
      vm_resize_pages_internal(vm, old_addr, old_size, new_size);
      break;
    default:
      unreachable;
  }
  return 0;
}


page_t *vm_getpage(vm_mapping_t *vm, size_t off) {
  unimplemented("vm_getpage");
  return NULL;
}

int vm_putpage(vm_mapping_t *vm, size_t off, page_t *page) {
  ASSERT(vm->type == VM_TYPE_PAGE);
  unimplemented("vm_putpage");
  return 0;
}

//

vm_mapping_t *vm_get_mapping(uintptr_t virt_addr) {
  if (virt_addr == 0)
    return NULL;

  address_space_t *space;
  if (space_contains(PERCPU_ADDRESS_SPACE, virt_addr)) {
    space = PERCPU_ADDRESS_SPACE;
  } else {
    space = kernel_space;
  }

  SPIN_LOCK(&space->lock);
  vm_mapping_t *vm = intvl_tree_get_point(space->new_tree, virt_addr);
  SPIN_UNLOCK(&space->lock);
  return vm;
}

uintptr_t vm_virt_to_phys(uintptr_t virt_addr) {
  vm_mapping_t *vm = vm_get_mapping(virt_addr);
  if (!vm)
    return 0;

  return vm_mapping_to_phys(vm, virt_addr);
}

uintptr_t vm_mapping_to_phys(vm_mapping_t *vm, uintptr_t virt_addr) {
  if (vm->type == VM_TYPE_RSVD)
    return 0;

  size_t off = virt_addr - vm->address;
  if (vm->type == VM_TYPE_PHYS) {
    return vm->vm_phys + off;
  } else if (vm->type == VM_TYPE_PAGE) {
    if (vm->flags & VM_CONTIG) {
      // we can take a shortcut here if we know the pages are contiguous
      return vm->vm_pages->address + off;
    }

    // walk the page list and find the page that contains the address
    page_t *page = vm->vm_pages;
    uintptr_t curr_addr = vm->address;
    while (curr_addr < virt_addr) {
      size_t sz = pg_flags_to_size(page->flags);
      if (curr_addr + sz > virt_addr) {
        // the pointer is within this page
        return page->address + (virt_addr - curr_addr);
      }

      page = page->next;
      curr_addr += sz;
    }
    return 0;
  }

  unreachable;
}

//
// vmalloc api

static inline bool is_vmalloc_mapping(vm_mapping_t *vm) {
  return vm->type == VM_TYPE_PAGE && (vm->flags & VM_MALLOC);
}

// TODO: propagate failures instead of panicing

void *vmalloc(size_t size, uint32_t pg_flags) {
  if (size == 0)
    return NULL;

  // TODO: the pages dont have to be contiguous so we can improve
  //       the allocation strategy here. for now this is the same
  //       as vmalloc_phys

  // allocate pages
  page_t *pages = alloc_pages(SIZE_TO_PAGES(size), pg_flags);
  PANIC_IF(!pages, "vmalloc: alloc_pages failed");

  // allocate and map the virtual memory
  uint32_t vm_flags = VM_GUARD | VM_MALLOC;
  if (pg_flags & PG_USER)
    vm_flags |= VM_USER;

  vm_mapping_t *vm = vm_alloc_pages(pages, 0, size, vm_flags, "vmalloc");
  PANIC_IF(!vm, "vmalloc: vm_alloc_pages failed");

  void *ptr = vm_map(vm, pg_flags);
  PANIC_IF(!ptr, "vmalloc: vm_map failed")
  return ptr;
}

void *vmalloc_phys(size_t size, uint32_t pg_flags) {
  if (size == 0)
    return NULL;

  // allocate pages
  page_t *pages = alloc_pages(SIZE_TO_PAGES(size), pg_flags);
  PANIC_IF(!pages, "vmalloc: alloc_pages failed");

  // allocate and map the virtual memory
  uint32_t vm_flags = VM_GUARD | VM_MALLOC | VM_CONTIG;
  if (pg_flags & PG_USER)
    vm_flags |= VM_USER;

  vm_mapping_t *vm = vm_alloc_pages(pages, 0, size, vm_flags, "vmalloc");
  PANIC_IF(!vm, "vmalloc_phys: vm_alloc_pages failed");

  void *ptr = vm_map(vm, pg_flags);
  PANIC_IF(!ptr, "vmalloc_phys: vm_map failed")
  return ptr;
}

void *vmalloc_at_phys(uintptr_t phys_addr, size_t size, uint32_t pg_flags) {
  if (size == 0)
    return NULL;

  // allocate pages
  page_t *pages = alloc_pages_at(phys_addr, SIZE_TO_PAGES(size), pg_flags);
  PANIC_IF(!pages, "vmalloc_at_phys: alloc_pages_at failed");

  // allocate and map the virtual memory
  uint32_t vm_flags = VM_GUARD | VM_MALLOC | VM_CONTIG;
  if (pg_flags & PG_USER)
    vm_flags |= VM_USER;

  vm_mapping_t *vm = vm_alloc_pages(pages, 0, size, vm_flags, "vmalloc");
  PANIC_IF(!vm, "vmalloc_at_phys: vm_alloc_pages failed");

  void *ptr = vm_map(vm, pg_flags);
  PANIC_IF(!ptr, "vmalloc_at_phys: vm_map failed")
  return ptr;
}

void vfree(void *ptr) {
  if (ptr == NULL)
    return;

  vm_mapping_t *vm = vm_get_mapping((uintptr_t) ptr);
  PANIC_IF(!vm, "vfree: invalid pointer: {:018p} is not mapped", ptr);
  PANIC_IF(!is_vmalloc_mapping(vm), "vfree: invalid pointer: {:018p} is not a vmalloc pointer", ptr);
  PANIC_IF(((uintptr_t)ptr) != vm->address, "vfree: invalid pointer: {:018p} is not the start of a vmalloc mapping", ptr);
  vm_free(vm);
}


//
// debug functions

void vm_print_mappings(address_space_t *space) {
  vm_mapping_t *prev = NULL;
  vm_mapping_t *vm = LIST_FIRST(&space->mappings);
  while (vm) {
    size_t extra_size = vm->virt_size - vm->size;
    if ((vm->flags & VM_GUARD) && (vm->flags & VM_STACK)) {
      // in a stack mapping the guard page comes first in memory
      // since it is at the logical end or bottom of the stack
      kprintf("  [%018p-%018p] {:$ >10llu} guard\n",
              vm->address-extra_size, vm->address, extra_size);
    }

    kprintf("  [{:018p}-{:018p}] {:$ >10llu} {:str}\n",
            vm->address, vm->address+vm->size, vm->size, &vm->name);

    if ((vm->flags & VM_GUARD) && !(vm->flags & VM_STACK)) {
      kprintf("  [%018p-%018p] {:$ >10llu} guard\n",
              vm->address+vm->size, vm->address+vm->size+extra_size, extra_size);
    }
    vm = LIST_NEXT(vm, list);
  }
}

void vm_print_space_tree_graphiz(address_space_t *space) {
  intvl_iter_t *iter = intvl_iter_tree(space->new_tree);
  intvl_node_t *node;
  rb_node_t *nil = space->new_tree->tree->nil;
  int null_count = 0;

  kprintf("digraph BST {\n");
  kprintf("  node [fontname=\"Arial\"];\n");
  while ((node = intvl_iter_next(iter))) {
    interval_t i = node->interval;
    rb_node_t *rbnode = node->node;

    vm_mapping_t *vm = node->data;
    kprintf("  %llu [label=\"{:str}\\n%p-%p\"];\n",
            rbnode->key, &vm->name, i.start, i.end);

    if (rbnode->left != nil) {
      kprintf("  %llu -> %llu\n", rbnode->key, rbnode->left->key);
    } else {
      kprintf("  null%d [shape=point];\n", null_count);
      kprintf("  %llu -> null%d;\n", rbnode->key, null_count);
      null_count++;
    }

    if (rbnode->right != nil) {
      kprintf("  %llu -> %llu\n", rbnode->key, rbnode->right->key);
    } else {
      kprintf("  null%d [shape=point];\n", null_count);
      kprintf("  %llu -> null%d;\n", rbnode->key, null_count);
      null_count++;
    }
  }
  kprintf("}\n");
  kfree(iter);
}

void vm_print_address_space() {
  kprintf("vm: address space mappings\n");
  kprintf("{:$=^80s}\n", " user space ");
  vm_print_mappings(PERCPU_ADDRESS_SPACE);
  kprintf("{:$=^80s}\n", " kernel space ");
  vm_print_mappings(kernel_space);
  kprintf("{:$=^80}\n");
}
