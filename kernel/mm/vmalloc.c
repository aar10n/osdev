//
// Created by Aaron Gill-Braun on 2022-06-17.
//

#include <kernel/mm/vmalloc.h>
#include <kernel/mm/pmalloc.h>
#include <kernel/mm/pgtable.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/init.h>

#include <kernel/cpu/cpu.h>
#include <kernel/debug/debug.h>

#include <kernel/init.h>
#include <kernel/irq.h>
#include <kernel/panic.h>
#include <kernel/string.h>
#include <kernel/printf.h>

#include <interval_tree.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(x, ...) kprintf(x, ##__VA_ARGS__)
#define PANIC_IF(x, msg, ...) { if (x) panic(msg, ##__VA_ARGS__); }

#define INTERNAL_PG_FLAGS 0xF00

// these are the default hints for different combinations of vm flags
// they are used as a starting point for the kernel when searching for
// a free region
#define HINT_USER_DEFAULT   0x0000000040000000ULL // for VM_USER
#define HINT_USER_MALLOC    0x0000010000000000ULL // for VM_USER|VM_MALLOC
#define HINT_USER_STACK     0x0000800000000000ULL // for VM_USER|VM_STACK
#define HINT_KERNEL_DEFAULT 0xFFFFC00000000000ULL // for no flags
#define HINT_KERNEL_MALLOC  0xFFFFC01000000000ULL // for VM_MALLOC
#define HINT_KERNEL_STACK   0xFFFFFF8040000000ULL // for VM_STACK

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

static inline const char *prot_to_debug_str(uint32_t vm_flags) {
  if ((vm_flags & VM_PROT_MASK) == 0)
    return "---";

  if (vm_flags & VM_READ) {
    if (vm_flags & VM_WRITE) {
      if (vm_flags & VM_EXEC) {
        return "rwe";
      }
      return "rw-";
    } else if (vm_flags & VM_EXEC) {
      return "r-x";
    }
    return "r--";
  }
  return "bad";
}

static always_inline uint32_t vm_flags_to_pg_flags(uint32_t vm_flags) {
  uint32_t pg_flags = 0;
  if (vm_flags & VM_WRITE) pg_flags |= PG_WRITE;
  if (vm_flags & VM_USER) pg_flags |= PG_USER;
  if (vm_flags & VM_EXEC) pg_flags |= PG_EXEC;
  if (vm_flags & VM_NOCACHE) pg_flags |= PG_NOCACHE;
  if (vm_flags & VM_HUGE_2MB) {
    pg_flags |= PG_BIGPAGE;
  } else if (vm_flags & VM_HUGE_1GB) {
    pg_flags |= PG_HUGEPAGE;
  }
  return pg_flags;
}

static always_inline bool space_contains(address_space_t *space, uintptr_t addr) {
  return addr >= space->min_addr && addr < space->max_addr;
}

static always_inline interval_t mapping_interval(vm_mapping_t *vm) {
  // if the mapping is a stack mapping the interval base address is one page below
  //  the vm address to account for the added guard page
  if (vm->flags & VM_STACK)
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
    if (vm_flags & VM_STACK)
      return HINT_USER_STACK;
    if (vm_flags & VM_MALLOC)
      return HINT_USER_MALLOC;
    return HINT_USER_DEFAULT;
  } else {
    if (vm_flags & VM_STACK)
      return HINT_KERNEL_STACK;
    if (vm_flags & VM_MALLOC)
      return HINT_KERNEL_MALLOC;
    return HINT_KERNEL_DEFAULT;
  }
}

//
// MARK: Mapping type functions
//

// phys type

static void phys_map_internal(vm_mapping_t *vm, uintptr_t phys, size_t size, size_t off) {
  uint32_t pg_flags = vm_flags_to_pg_flags(vm->flags);
  size_t stride = vm_flags_to_size(vm->flags);
  ASSERT(off % stride == 0);
  ASSERT(off + size <= vm->size);

  size_t count = size / stride;
  uintptr_t ptr = vm->address + off;
  uintptr_t phys_ptr = phys + off;
  while (count > 0) {
    page_t *table_pages = NULL;
    recursive_map_entry(ptr, phys_ptr, pg_flags, &table_pages);
    ptr += stride;
    phys_ptr += stride;
    count--;

    if (table_pages != NULL) {
      page_t *last_page = SLIST_GET_LAST(table_pages, next);
      SLIST_ADD_SLIST(&vm->space->table_pages, table_pages, last_page, next);
    }
  }

  cpu_flush_tlb();
}

static void phys_unmap_internal(vm_mapping_t *vm, size_t size, size_t off) {
  uint32_t pg_flags = vm_flags_to_pg_flags(vm->flags);
  size_t stride = vm_flags_to_size(vm->flags);
  ASSERT(off % stride == 0);
  ASSERT(off + size <= vm->size);

  size_t count = size / stride;
  uintptr_t ptr = vm->address + off;
  while (count > 0) {
    recursive_unmap_entry(ptr, pg_flags);
    ptr += stride;
    count--;
  }

  cpu_flush_tlb();
}

static page_t *phys_getpage_internal(vm_mapping_t *vm, size_t off) {
  ASSERT(off <= vm->size);
  // physical mappings are assumed to be reserved and so must be unmanaged
  return alloc_cow_pages_at(vm->vm_phys + off, 1, vm_flags_to_size(vm->flags));
}

// pages type

static void page_map_internal(vm_mapping_t *vm, page_t *pages, size_t size, size_t off) {
  uint32_t pg_flags = vm_flags_to_pg_flags(vm->flags);
  size_t stride = vm_flags_to_size(vm->flags);
  ASSERT(off % stride == 0);
  ASSERT(off + size <= vm->size);

  size_t count = size / stride;
  uintptr_t ptr = vm->address + off;
  page_t *curr = pages;
  while (curr != NULL) {
    if (count == 0) {
      if (curr->mapping == NULL) {
        // memory leak if these are unmapped pages that are not needed
        panic("more pages than needed to map region {:str}", &vm->name);
      }
      break;
    }

    // the page must be owned by the mapping if updating
    if (curr->mapping == NULL) {
      // mapping for the first time
      curr->flags &= INTERNAL_PG_FLAGS;
      curr->flags |= pg_flags | PG_PRESENT;
      curr->mapping = vm;
    } else if (curr->mapping == vm) {
      // updating existing mappings
      curr->flags &= INTERNAL_PG_FLAGS;
      curr->flags |= pg_flags | PG_PRESENT;
    }

    page_t *table_pages = NULL;
    recursive_map_entry(ptr, curr->address, pg_flags, &table_pages);
    // kprintf("mapped: %p [phys = %p, flags = %#08b]\n", ptr, curr->address, pg_flags | PG_PRESENT);
    ptr += stride;
    curr = curr->next;
    count--;

    if (table_pages != NULL) {
      page_t *last_page = SLIST_GET_LAST(table_pages, next);
      SLIST_ADD_SLIST(&vm->space->table_pages, table_pages, last_page, next);
    }
  }

  if (count > 0) {
    DPRINTF("not enough pages to map region {:str}\n", &vm->name);
  }

  cpu_flush_tlb();
}

static void page_unmap_internal(vm_mapping_t *vm, size_t size, size_t off) {
  uint32_t pg_flags = vm_flags_to_pg_flags(vm->flags);
  size_t stride = vm_flags_to_size(vm->flags);
  ASSERT(off % stride == 0);
  ASSERT(off + size <= vm->size);

  uintptr_t ptr = vm->address;
  page_t *curr = vm->vm_pages;
  while (off > 0) {
    if (curr == NULL) {
      panic("page_unmap_internal: something went wrong");
    }
    // get to page at offset
    ptr += pg_flags_to_size(curr->flags);
    curr = curr->next;
    off -= stride;
  }

  uintptr_t max_ptr = ptr + size;
  while (ptr < max_ptr && curr != NULL) {
    ASSERT(curr->mapping != NULL);
    recursive_unmap_entry(ptr, curr->flags);
    ptr += pg_flags_to_size(curr->flags);

    // dont free the pages until the mapping is destroyed
    if (curr->mapping == vm) {
      curr->mapping = NULL;
      curr->flags &= INTERNAL_PG_FLAGS;
    }
    curr = curr->next;
  }

  cpu_flush_tlb();
}

static page_t *page_getpage_internal(vm_mapping_t *vm, size_t off) {
  page_t *curr = vm->vm_pages;
  while (off > 0) {
    if (!curr) {
      return NULL;
    }

    size_t size = pg_flags_to_size(curr->flags);
    if (off < size) {
      break;
    }
    curr = curr->next;
  }
  return curr;
}

static page_t *page_split_internal(page_t *pages, size_t off) {
  ASSERT(pages->flags & PG_HEAD);
  page_t *curr = pages;
  size_t total_count = pages->head.count;

  page_t *prev = NULL;
  size_t count = 0;
  while (off > 0) {
    if (!curr) {
      return NULL;
    }

    size_t size = pg_flags_to_size(curr->flags);
    if (off < size) {
      break;
    }
    count++;
    off -= size;
    prev = curr;
    curr = curr->next;
  }

  if (count == 0) {
    return NULL;
  }

  ASSERT(curr != NULL);
  ASSERT(prev != NULL);

  pages->head.count = count;
  curr->flags |= PG_HEAD;
  curr->head.count = total_count - count;
  prev->next = NULL;
  return curr;
}

// file type

static void file_map_internal(vm_mapping_t *vm, vm_file_t *file, size_t size, size_t off) {
  uint32_t pg_flags = vm_flags_to_pg_flags(vm->flags);
  size_t stride = vm_flags_to_size(vm->flags);
  ASSERT(off % stride == 0);
  ASSERT(off + size <= vm->size);

  if (file->pages == NULL) {
    panic("file_map_internal: file pages not initialized");
  }

  size_t count = size / stride;
  uintptr_t ptr = vm->address + off;
  for (size_t i = 0; i < count; i++) {
    page_t *page = file->pages[i];
    if (page == NULL) {
      continue; // ignore holes
    }

    if (!(page->flags & PG_PRESENT)) {
      // the page is owned by the mapping
      ASSERT(page->mapping == NULL);
      page->flags &= INTERNAL_PG_FLAGS;
      page->flags |= pg_flags | PG_PRESENT;
      page->mapping = vm;
    }

    page_t *table_pages = NULL;
    recursive_map_entry(ptr, page->address, pg_flags, &table_pages);
    ptr += stride;

    if (table_pages != NULL) {
      page_t *last_page = SLIST_GET_LAST(table_pages, next);
      SLIST_ADD_SLIST(&vm->space->table_pages, table_pages, last_page, next);
    }
  }

  cpu_flush_tlb();
}

static void file_unmap_internal(vm_mapping_t *vm, size_t size, size_t off) {
  ASSERT(vm->type == VM_TYPE_FILE);
  vm_file_t *file = vm->vm_file;
  uint32_t pg_flags = vm_flags_to_pg_flags(vm->flags);
  size_t stride = vm_flags_to_size(vm->flags);
  ASSERT(off + size <= vm->size);

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

static page_t *file_getpage_internal(vm_mapping_t *vm, size_t off) {
  ASSERT(vm->type == VM_TYPE_FILE);
  vm_file_t *file = vm->vm_file;
  uint32_t pg_flags = vm_flags_to_pg_flags(vm->flags);
  size_t stride = vm_flags_to_size(vm->flags);
  ASSERT(off <= vm->size);
  return file->pages[off / stride];
}

static void file_putpages_internal(vm_mapping_t *vm, vm_file_t *file, size_t size, size_t off, page_t *pages) {
  uint32_t pg_flags = vm_flags_to_pg_flags(vm->flags);
  size_t stride = vm_flags_to_size(vm->flags);
  ASSERT(off % stride == 0);
  ASSERT(off + size <= vm->size);
  if (pages == NULL) {
    return;
  }

  uintptr_t ptr = vm->address + off;
  size_t index = off / stride;
  while (pages != NULL) {
    if (file->pages[index] != NULL) {
      panic("file_putpage_internal: page already mapped at offset %zu [vm={:str}]", index * stride, &vm->name);
    }

    page_t *curr = page_list_remove_head(&pages);
    page_t *table_pages = NULL;
    if (pg_flags_to_size(curr->flags) != stride) {
      panic("file_putpage_internal: page size does not match vm page size");
    }
    recursive_map_entry(ptr, curr->address, pg_flags, &table_pages);

    file->pages[index] = curr;
    curr->mapping = vm;
    ptr += stride;
    file->mapped_size += stride;
    index++;

    if (table_pages != NULL) {
      page_t *last_page = SLIST_GET_LAST(table_pages, next);
      SLIST_ADD_SLIST(&vm->space->table_pages, table_pages, last_page, next);
    }
  }

  cpu_flush_tlb();
}

// MARK: Virtual space allocation

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
  off_t delta = (off_t)(new_size - vm->size);
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

    intvl_tree_update_interval(space->new_tree, node, -delta, 0);
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

    intvl_tree_update_interval(space->new_tree, node, 0, delta);
    vm->size = new_size;
  }

  return true;
}

static vm_mapping_t *split_mapping(vm_mapping_t *vm, size_t off) {
  ASSERT(off % vm_flags_to_size(vm->flags) == 0);
  // vm should be locked while calling this
  address_space_t *space = vm->space;
  intvl_node_t *node = intvl_tree_find(space->new_tree, mapping_interval(vm));
  ASSERT(node && node->data == vm);
  ASSERT(off < vm->size);

  // create new mapping
  vm_mapping_t *new_vm = kmallocz(sizeof(vm_mapping_t));
  new_vm->type = vm->type;
  new_vm->flags = vm->flags | VM_SPLIT;
  new_vm->address = vm->address + off;
  new_vm->size = vm->size - off;
  new_vm->space = space;
  new_vm->name = str_copy_cstr(cstr_from_str(vm->name));
  spin_init(&new_vm->lock);

  vm->flags |= VM_LINKED;
  vm->size = off;
  if (vm->flags & VM_STACK) {
    // unmapped virtual space stays at bottom of the region
    new_vm->virt_size = new_vm->size;
  } else {
    // unmapped virtual space moves to new mapping at top of the region
    new_vm->virt_size = vm->virt_size - vm->size;
    vm->virt_size = vm->size;
  }

  SPIN_LOCK(&space->lock);
  // resize current interval down and insert new node
  interval_t intvl = mapping_interval(vm);
  off_t delta_end = (off_t)(node->interval.end - intvl.end);
  intvl_tree_update_interval(space->new_tree, node, 0, -delta_end);
  intvl_tree_insert(space->new_tree, mapping_interval(new_vm), new_vm);
  space->num_mappings++;
  ASSERT(contiguous(mapping_interval(vm), mapping_interval(new_vm)));

  // insert new node into the list
  LIST_INSERT(&space->mappings, new_vm, list, vm);
  SPIN_UNLOCK(&space->lock);
  return new_vm;
}

static bool move_mapping(vm_mapping_t *vm, size_t newsize) {
  // space should be locked while calling this
  address_space_t *space = vm->space;
  uintptr_t base = vm->address;
  size_t virt_size = newsize;

  size_t off = 0;
  if (vm->flags & VM_STACK) {
    virt_size += PAGE_SIZE;
    off = PAGE_SIZE;
    base -= virt_size;
  }

  // look for a new free region
  vm_mapping_t *closest = NULL;
  uintptr_t virt_addr = get_free_region(space, base, virt_size, vm_flags_to_size(vm->flags), vm->flags, &closest);
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
// MARK: Public API
//

static always_inline bool can_handle_fault(vm_mapping_t *vm, uintptr_t fault_addr, uint32_t error) {
  return vm->type == VM_TYPE_FILE;
}

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
    if (vm == NULL || !can_handle_fault(vm, fault_addr, error_code)) {
      // TODO: support extending stacks automatically if the fault happens
      //       in the guard page
      goto exception;
    }

    DPRINTF("non-present page fault in vm_file [vm={:str},addr=%p]\n", &vm->name, fault_addr);
    vm_file_t *file = vm->vm_file;
    size_t off = fault_addr - vm->address;
    page_t *page = file->get_page(vm, off, vm->flags, file->data);
    if (!page) {
      DPRINTF("failed to get non-present page in vm_file [vm={:str},off=%zu]\n", &vm->name, off);
      goto exception;
    }

    // map the new page into the file
    size_t size = vm_flags_to_size(vm->flags);
    file_putpages_internal(vm, vm->vm_file, size, off, page);
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

  irq_register_exception_handler(CPU_EXCEPTION_PF, page_fault_handler);

  // set up the starting address space layout
  size_t lowmem_size = kernel_address;
  size_t kernel_code_size = kernel_code_end - kernel_code_start;
  size_t kernel_data_size = kernel_data_end - kernel_code_end;
  size_t reserved_size = kernel_reserved_va_ptr - KERNEL_RESERVED_VA;

  vmap_rsvd(0, PAGE_SIZE, VM_USER | VM_FIXED, "null");
  vmap_phys(0, kernel_virtual_offset, lowmem_size, VM_FIXED, "reserved")->flags
    |= VM_READ | VM_WRITE;
  vmap_phys(kernel_address, kernel_code_start, kernel_code_size, VM_FIXED, "kernel code")->flags
    |= VM_READ | VM_EXEC;
  vmap_phys(kernel_address + kernel_code_size, kernel_code_end, kernel_data_size, VM_FIXED, "kernel data")->flags
    |= VM_READ | VM_WRITE;
  vmap_phys(kheap_phys_addr(), KERNEL_HEAP_VA, KERNEL_HEAP_SIZE, VM_FIXED, "kernel heap")->flags
    |= VM_READ | VM_WRITE;
  vmap_phys(kernel_reserved_start, KERNEL_RESERVED_VA, reserved_size, VM_FIXED, "kernel reserved")->flags
    |= VM_READ | VM_WRITE;

  // bsp kernel stack
  page_t *stack_pages = alloc_pages(SIZE_TO_PAGES(KERNEL_STACK_SIZE));
  vm_mapping_t *stack_vm = vmap_pages(stack_pages, 0, KERNEL_STACK_SIZE, VM_WRITE | VM_STACK, "kernel stack");

  execute_init_address_space_callbacks();

  // relocate boot info struct
  static_assert(sizeof(boot_info_v2_t) <= PAGE_SIZE);
  vm_mapping_t *bootinfo_vm = vmap_phys((uintptr_t) boot_info_v2, 0, PAGE_SIZE, VM_WRITE, "boot info");
  boot_info_v2 = (void *) bootinfo_vm->address;

  vm_print_address_space();

  // switch to new kernel stack
  kprintf("switching to new kernel stack\n");
  uint64_t rsp = cpu_read_stack_pointer();
  uint64_t stack_offset = ((uint64_t) &entry_initial_stack_top) - rsp;

  uint64_t new_rsp = stack_vm->address + KERNEL_STACK_SIZE - stack_offset;
  memcpy((void *) new_rsp, (void *) rsp, stack_offset);
  cpu_write_stack_pointer(new_rsp);
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

  vmap_rsvd(0, PAGE_SIZE, VM_USER | VM_FIXED, "null");
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

vm_file_t *vm_file_alloc(size_t size, vm_getpage_t fn, void *data) {
  vm_file_t *file = kmalloc(sizeof(vm_file_t));
  file->full_size = size;
  file->get_page = fn;
  file->data = data;

  size_t num_pages = size / PAGE_SIZE;
  size_t arrsz = num_pages * sizeof(page_t *);
  if (arrsz >= PAGE_SIZE) {
    file->pages = vmalloc(arrsz, 0);
  } else {
    file->pages = kmalloc(arrsz);
  }
  memset(file->pages, 0, arrsz);
  return file;
}

void vm_file_free(vm_file_t *file) {
  size_t num_pages = file->full_size / PAGE_SIZE;
  size_t arrsz = num_pages * sizeof(page_t *);
  if (file->pages != NULL) {
    // free the pages and array
    for (size_t i = 0; i < num_pages; i++) {
      if (file->pages[i] != NULL) {
        free_pages(file->pages[i]);
      }
    }

    if (arrsz >= PAGE_SIZE) {
      vfree(file->pages);
    } else {
      kfree(file->pages);
    }
    file->pages = NULL;
  }
  kfree(file);
}

//
// MARK: vmap api
//

vm_mapping_t *vmap(enum vm_type type, uintptr_t hint, size_t size, uint32_t vm_flags, const char *name, void *arg) {
  ASSERT(type < VM_MAX_TYPE);
  if (size == 0) {
    return NULL;
  }

  if (vm_flags & VM_WRITE || vm_flags & VM_EXEC) {
    // if no protection flags are specified it means the region is not mapped
    // but if any protection is given the region must be readable
    vm_flags |= VM_READ;
  }

  size_t pgsize = PAGE_SIZE;
  if (vm_flags & VM_HUGE_2MB) {
    pgsize = PAGE_SIZE_2MB;
  } else if (vm_flags & VM_HUGE_1GB) {
    pgsize = PAGE_SIZE_1GB;
  }

  if (vm_flags & VM_FIXED && !is_aligned(hint, pgsize)) {
    kprintf("vmap: hint %p is not aligned to page size %zu [name=%s]\n", hint, pgsize, name);
    return NULL;
  }

  vm_mapping_t *vm = kmallocz(sizeof(vm_mapping_t));
  vm->type = type;
  vm->flags = vm_flags;
  vm->virt_size = size;
  vm->size = size;
  spin_init(&vm->lock);

  size_t off = 0;
  if (vm_flags & VM_STACK) {
    vm->virt_size += PAGE_SIZE;
    off = PAGE_SIZE;
  }

  address_space_t *space;
  if (vm_flags & VM_USER) {
    space = PERCPU_ADDRESS_SPACE;
  } else {
    space = kernel_space;
  }

  // allocate the virtual address space for the mapping
  SPIN_LOCK(&space->lock);
  uintptr_t virt_addr = 0;
  vm_mapping_t *closest = NULL;
  if (vm_flags & VM_FIXED) {
    if (!space_contains(space, hint)) {
      panic("vmap: hint address not in address space: %p [name=%s]\n", hint, name);
    }

    if (vm_flags & VM_STACK) {
      if (hint < vm->virt_size) {
        SPIN_UNLOCK(&space->lock);
        kfree(vm);
        panic("vmap: hint address is too low for requested stack size [name=%s]\n", name);
      }
      hint -= vm->virt_size;
    }
    virt_addr = hint;

    // make sure the requested range is free
    if (!check_range_free(space, hint, vm->virt_size, vm_flags, &closest)) {
      SPIN_UNLOCK(&space->lock);
      kfree(vm);
      kprintf("vmap: requested fixed address range is not free %p-%p [name=%s]\n", hint, hint + vm->virt_size, name);
      return NULL;
    }
  } else {
    // dynamically allocated
    hint = choose_best_hint(space, hint, vm_flags);
    if (vm_flags & VM_STACK) {
      ASSERT(hint > vm->virt_size);
      hint -= vm->virt_size;
    }

    virt_addr = get_free_region(space, hint, vm->virt_size, pgsize, vm_flags, &closest);
    if (virt_addr == 0) {
      SPIN_UNLOCK(&space->lock);
      kfree(vm);
      kprintf("vmap: failed to satisfy allocation request [name=%s]\n", name);
      return NULL;
    }
  }

  vm->address = virt_addr + off;
  vm->name = str_make(name);
  vm->space = space;
  switch (vm->type) {
    case VM_TYPE_RSVD: vm->flags &= ~VM_PROT_MASK; break;
    case VM_TYPE_PHYS: vm->vm_phys = (uintptr_t) arg; break;
    case VM_TYPE_PAGE: vm->vm_pages = (page_t *) arg; break;
    case VM_TYPE_FILE: vm->vm_file = (vm_file_t *) arg; break;
    default:
      unreachable;
  }

  // insert mapping into to the mappings list
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

  // insert mapping to address space tree
  intvl_tree_insert(space->new_tree, mapping_interval(vm), vm);
  space->num_mappings++;

  // map the region if any protection flags are given
  if (vm->flags & VM_PROT_MASK) {
    switch (vm->type) {
      case VM_TYPE_RSVD:
        break;
      case VM_TYPE_PHYS:
        phys_map_internal(vm, vm->vm_phys, vm->size, 0);
        break;
      case VM_TYPE_PAGE:
        page_map_internal(vm, vm->vm_pages, vm->size, 0);
        break;
      case VM_TYPE_FILE:
        file_map_internal(vm, vm->vm_file, vm->size,  0);
        break;
      default:
        unreachable;
    }
    vm->flags |= VM_MAPPED;
  }
  SPIN_UNLOCK(&space->lock);
  return vm;
}

void vmap_free(vm_mapping_t *vm) {
  ASSERT(vm->type != VM_TYPE_RSVD);
  vm_mapping_t *linked = NULL;
  if (vm->flags & VM_MAPPED) {
    // unmap the region
    switch (vm->type) {
      case VM_TYPE_RSVD:
        break;
      case VM_TYPE_PHYS:
        phys_unmap_internal(vm, vm->size, 0);
        break;
      case VM_TYPE_PAGE:
        page_unmap_internal(vm, vm->size, 0);
        free_pages(vm->vm_pages);
        if (vm->flags & VM_LINKED)
          LIST_NEXT(vm, list);
        break;
      case VM_TYPE_FILE:
        file_unmap_internal(vm, vm->size, 0);
        vm_file_free(vm->vm_file);
        break;
      default:
        unreachable;
    }
    vm->flags &= ~VM_MAPPED;
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

  if (linked) {
    linked->flags &= ~VM_SPLIT;
    vmap_free(linked);
  }
}

vm_mapping_t *vmap_rsvd(uintptr_t hint, size_t size, uint32_t vm_flags, const char *name) {
  vm_mapping_t *vm = vmap(VM_TYPE_RSVD, hint, size, vm_flags, name, NULL);
  PANIC_IF(!vm, "vmap: failed to make reserved mapping %s\n", name);
  return vm;
}

vm_mapping_t *vmap_phys(uintptr_t phys_addr, uintptr_t hint, size_t size, uint32_t vm_flags, const char *name) {
  vm_mapping_t *vm = vmap(VM_TYPE_PHYS, hint, size, vm_flags, name, (void *) phys_addr);
  PANIC_IF(!vm, "vmap: failed to make physical address mapping %s [phys=%p]\n", name, phys_addr);
  return vm;
}

vm_mapping_t *vmap_pages(page_t *pages, uintptr_t hint, size_t size, uint32_t vm_flags, const char *name) {
  vm_mapping_t *vm = vmap(VM_TYPE_PAGE, hint, size, vm_flags, name, pages);
  PANIC_IF(!vm, "vmap: failed to make pages mapping %s [page=%p]\n", name, pages);
  return vm;
}

vm_mapping_t *vmap_file(vm_file_t *file, uintptr_t hint, size_t size, uint32_t vm_flags, const char *name) {
  vm_mapping_t *vm = vmap(VM_TYPE_FILE, hint, size, vm_flags, name, file);
  PANIC_IF(!vm, "vmap: failed to make file mapping %s [file=%p]\n", name, file);
  return vm;
}

//

int vm_resize(vm_mapping_t *vm, size_t new_size, bool allow_move) {
  if (vm->type != VM_TYPE_PAGE && vm->type != VM_TYPE_FILE) {
    kprintf("vm_resize: invalid mapping type %d [name={:str}]\n", vm->type, &vm->name);
    return -1;
  } else if (vm->flags & VM_LINKED || vm->flags & VM_SPLIT) {
    kprintf("vm_resize: cannot resize part of a split mapping [name={:str}]\n", &vm->name);
    return -1;
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

  address_space_t *space = vm->space;
  SPIN_LOCK(&space->lock);
  bool ok = move_mapping(vm, new_size);
  SPIN_UNLOCK(&space->lock);
  SPIN_UNLOCK(&vm->lock);
  if (!ok) {
    return -1;
  }

  // finally call the appropriate resize function to update the underlying mappings
LABEL(update_memory);
  if (new_size < old_size) {
    size_t len = old_size - new_size;
    size_t off = new_size;
    if (vm->type == VM_TYPE_PAGE) {
      page_unmap_internal(vm, len, off);
    } else if (vm->type == VM_TYPE_FILE) {
      file_unmap_internal(vm, len, off);
    }
  }
  return 0;
}

int vm_update(vm_mapping_t *vm, size_t off, size_t len, uint32_t prot_flags) {
  if (vm->type != VM_TYPE_PAGE) {
    DPRINTF("vm_update: error: invalid mapping type [type=%d, name={:str}]\n", vm->type, &vm->name);
    return -1;
  } else if (off + len > vm->size) {
    DPRINTF("vm_update: error: offset is out of bounds [off=%#zx, name={:str}]\n", off, &vm->name);
    return -1;
  } else if (off % vm_flags_to_size(vm->flags) != 0) {
    DPRINTF("vm_update: error: offset must be page aligned [off=%#zx, name={:str}]\n", off, &vm->name);
    return -1;
  } else if (len % vm_flags_to_size(prot_flags) != 0) {
    DPRINTF("vm_update: error: length must be page aligned [len=%#zx, name={:str}]\n", len, &vm->name);
    return -1;
  } else if (len == 0) {
    return 0;
  }

  prot_flags &= VM_PROT_MASK;
  if (prot_flags == (vm->flags & VM_PROT_MASK)) {
    return 0; // nothing to change
  }

  if (off == 0 && len == vm->size) {
    // update the whole mapping
    SPIN_LOCK(&vm->lock);
    vm->flags &= ~VM_PROT_MASK;
    if (prot_flags == 0) {
      // unmap the whole mapping
      page_unmap_internal(vm, len, off);
      vm->flags &= ~VM_MAPPED;
      vm->flags |= prot_flags;
    } else {
      // update the protection flags
      vm->flags |= prot_flags | VM_MAPPED;
      page_map_internal(vm, vm->vm_pages, len, off);
    }
    SPIN_UNLOCK(&vm->lock);
    return 0;
  }

  // split the mapping at the offset where the protection flags change
  SPIN_LOCK(&vm->lock);
  vm_mapping_t *new_vm;
  vm_mapping_t *target_vm;
  if (off == 0) {
    // we are splitting and changing vm
    //   |-----------vm-----------|
    //   |---vm---|-----new_vm----|
    //   ^ 0
    new_vm = split_mapping(vm, len);
    new_vm->vm_pages = page_split_internal(vm->vm_pages, len);
    target_vm = vm;
  } else {
    new_vm = split_mapping(vm, off);
    new_vm->vm_pages = page_split_internal(vm->vm_pages, off);
    target_vm = new_vm;
    off = 0;
    if (new_vm->size > len) {
      // if the updated region does not cover the entire mapping, split it again
      // at the end of the updated region and set the flags to be the same as the
      // original mapping.
      //     |-----------vm-----------|
      //     |---vm---|-----new_vm----|
      //     |--vm--|--new_vm--|--vm--|
      vm_mapping_t *new_vm2 = split_mapping(new_vm, len);
      new_vm2->vm_pages = page_split_internal(new_vm->vm_pages, len);
    }
  }

  target_vm->flags &= ~VM_PROT_MASK;
  target_vm->flags |= prot_flags;

  // TODO: if the mapping has been split from another mapping check the
  //       other mapping to see if it can be merged with the new mapping
  page_t *page = page_getpage_internal(target_vm, off);
  page_map_internal(target_vm, page, len, off);
  SPIN_UNLOCK(&vm->lock);
  return 0;
}

page_t *vm_getpage(vm_mapping_t *vm, size_t off, bool cow) {
  page_t *page = NULL;
  switch (vm->type) {
    case VM_TYPE_RSVD:
      return NULL;
    case VM_TYPE_PHYS:
      if (!cow) return NULL;
      // only cow pages can be obtained from physical mappings
      return alloc_cow_pages_at(vm->vm_phys + off, 1, vm_flags_to_size(vm->flags));
    case VM_TYPE_PAGE:
      page = page_getpage_internal(vm, off);
      break;
    case VM_TYPE_FILE:
      page = file_getpage_internal(vm, off);
      break;
    default:
      unreachable;
  }

  if (cow)
    return alloc_cow_page(page);
  return page;
}

int vm_putpages(vm_mapping_t *vm, page_t *pages, size_t off) {
  ASSERT(!(vm->flags & VM_LINKED)); // should be end of the chain
  ASSERT(pages->flags & PG_HEAD);
  size_t pgsize = pg_flags_to_size(pages->flags);
  size_t size = pages->head.count * pgsize;
  if (off + size > vm->size) {
    DPRINTF("vm_putpages: out of bounds [vm={:str}, off=%zu, size=%zu]\n", &vm->name, off, size);
    return -1;
  }

  if (vm->type == VM_TYPE_PAGE) {
    page_map_internal(vm, pages, size, off);
  } else if (vm->type == VM_TYPE_FILE) {
    file_putpages_internal(vm, vm->vm_file, off, size, pages);
  } else {
    panic("vm_putpages: invalid mapping type");
  }
  return 0;
}

uintptr_t vm_mapping_to_phys(vm_mapping_t *vm, uintptr_t virt_addr) {
  if (vm->type == VM_TYPE_RSVD)
    return 0;

  size_t off = virt_addr - vm->address;
  if (vm->type == VM_TYPE_PHYS) {
    return vm->vm_phys + off;
  } else if (vm->type == VM_TYPE_PAGE) {
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

//
// MARK: vmalloc api
//

static vm_mapping_t *vmalloc_internal(size_t size, uint32_t vm_flags, const char *name) {
  if (size == 0)
    return NULL;
  size = align(size, PAGE_SIZE);

  vm_flags &= VM_FLAGS_MASK;
  vm_flags |= VM_MALLOC;
  if (!(vm_flags & VM_PROT_MASK)) {
    vm_flags |= VM_READ | VM_WRITE; // default to read/write
  }

  // allocate pages
  page_t *pages;
  size_t pagesize = vm_flags_to_size(vm_flags);
  if (pagesize == PAGE_SIZE) {
    pages = alloc_pages(SIZE_TO_PAGES(size));
    // pages = alloc_pages_mixed(SIZE_TO_PAGES(size));
  } else {
    pages = alloc_pages_size(SIZE_TO_PAGES(size), pagesize);
  }
  PANIC_IF(!pages, "vmalloc: alloc_pages failed");
  // allocate and map the virtual memory
  vm_mapping_t *vm = vmap_pages(pages, 0, size, vm_flags, name);
  PANIC_IF(!vm, "vmalloc: vmap_pages failed");
  return vm;
}

void *vmalloc(size_t size, uint32_t vm_flags) {
  vm_mapping_t *vm = vmalloc_internal(size, vm_flags, "vmalloc");
  return (void *) vm->address;
}

void *vmalloc_n(size_t size, uint32_t vm_flags, const char *name) {
  vm_mapping_t *vm = vmalloc_internal(size, vm_flags, name);
  str_free(&vm->name);
  vm->name = str_make(name);
  return (void *) vm->address;
}

void *vmalloc_at_phys(uintptr_t phys_addr, size_t size, uint32_t vm_flags) {
  if (size == 0)
    return NULL;

  vm_flags &= VM_FLAGS_MASK;
  vm_flags |= VM_MALLOC;
  if (!(vm_flags & VM_PROT_MASK)) {
    vm_flags |= VM_READ | VM_WRITE; // default to read/write
  }

  // allocate pages
  page_t *pages = alloc_pages_at(phys_addr, SIZE_TO_PAGES(size), vm_flags_to_size(vm_flags));
  PANIC_IF(!pages, "vmalloc_at_phys: alloc_pages_at failed");
  // allocate and map the virtual memory
  vm_mapping_t *vm = vmap_pages(pages, 0, size, vm_flags, "vmalloc");
  PANIC_IF(!vm, "vmalloc_at_phys: vmap_pages failed");
  return (void *) vm->address;
}

void vfree(void *ptr) {
  if (ptr == NULL)
    return;

  vm_mapping_t *vm = vm_get_mapping((uintptr_t) ptr);
  PANIC_IF(!vm, "vfree: invalid pointer: {:018p} is not mapped", ptr);
  PANIC_IF(!(vm->type == VM_TYPE_PAGE && (vm->flags & VM_MALLOC)), "vfree: invalid pointer: {:018p} is not a vmalloc pointer", ptr);
  PANIC_IF(((uintptr_t)ptr) != vm->address, "vfree: invalid pointer: {:018p} is not the start of a vmalloc mapping", ptr);
  vmap_free(vm);
}


//
// debug functions

void vm_print_mappings(address_space_t *space) {
  vm_mapping_t *prev = NULL;
  vm_mapping_t *vm = LIST_FIRST(&space->mappings);
  while (vm) {
    size_t extra_size = vm->virt_size - vm->size;
    if (vm->flags & VM_STACK) {
      // in a stack mapping the guard page comes first in memory
      // since it is at the logical end or bottom of the stack
      kprintf("  [%018p-%018p] {:$ >10llu}  ---  guard\n",
              vm->address-extra_size, vm->address, extra_size);
    }

    kprintf("  [{:018p}-{:018p}] {:$ >10llu}  %.3s  {:str}\n",
            vm->address, vm->address+vm->size, vm->size, prot_to_debug_str(vm->flags), &vm->name);
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
