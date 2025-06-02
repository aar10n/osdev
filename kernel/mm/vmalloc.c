//
// Created by Aaron Gill-Braun on 2022-06-17.
//

#include <kernel/mm/vmalloc.h>
#include <kernel/mm/pmalloc.h>
#include <kernel/mm/pgtable.h>
#include <kernel/mm/file.h>
#include <kernel/mm/heap.h>
#include <kernel/mm/init.h>

#include <kernel/cpu/cpu.h>
#include <kernel/debug/debug.h>

#include <kernel/init.h>
#include <kernel/fs.h>
#include <kernel/proc.h>
#include <kernel/panic.h>
#include <kernel/string.h>
#include <kernel/printf.h>

#include <interval_tree.h>

#include <abi/mman.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(x, ...) kprintf(x, ##__VA_ARGS__)
#define EPRINTF(fmt, ...) kprintf("vmalloc: %s: " fmt, __func__, ##__VA_ARGS__)
#define DPANICF(x, ...) panic(x, ##__VA_ARGS__)
#define ALLOC_ERROR(msg, ...) panic(msg, ##__VA_ARGS__)

#define do_align(x, al) ((al) > 0 ? (align(x, al)) : (x))

// these are the default hints for different combinations of vm flags
// they are used as a starting point for the kernel when searching for
// a free region
#define HINT_USER_DEFAULT   0x0000000050000000ULL // for VM_USER
#define HINT_USER_MALLOC    0x0000050000000000ULL // for VM_USER|VM_MALLOC
#define HINT_USER_STACK     0x0000800000000000ULL // for VM_USER|VM_STACK
#define HINT_KERNEL_DEFAULT 0xFFFFC00000000000ULL // for no flags
#define HINT_KERNEL_MALLOC  0xFFFFC01000000000ULL // for VM_MALLOC
#define HINT_KERNEL_STACK   0xFFFFFF8040000000ULL // for VM_STACK

extern uintptr_t entry_initial_stack_top;
address_space_t *default_user_space;
address_space_t *kernel_space;

// called from switch.asm
__used void switch_address_space(address_space_t *new_space) {
  address_space_t *current = curspace;
  if (current != NULL && current->page_table == new_space->page_table) {
    return;
  }
  set_curspace(new_space);
  set_current_pgtable(new_space->page_table);
}

static always_inline bool space_contains_addr(address_space_t *space, uintptr_t addr) {
  return addr >= space->min_addr && addr < space->max_addr;
}

static always_inline bool is_valid_pointer(uintptr_t ptr) {
  return (ptr >= USER_SPACE_START && ptr < USER_SPACE_END) ||
         (ptr >= KERNEL_SPACE_START && ptr < KERNEL_SPACE_END);
}

static always_inline bool is_valid_range(uintptr_t start, size_t len) {
  if (start >= KERNEL_SPACE_START) {
    return start + len <= KERNEL_SPACE_END;
  }
  return start + len <= USER_SPACE_END;
}

static always_inline address_space_t *select_space(address_space_t *user_space, uintptr_t addr) {
  if (addr >= KERNEL_SPACE_START) {
    return kernel_space;
  }
  return user_space;
}

static vm_mapping_t *space_get_mapping(address_space_t *space, uintptr_t vaddr) {
  space_lock_assert(space, MA_OWNED);
  vm_mapping_t *vm = intvl_tree_get_point(space->new_tree, vaddr);
  return vm;
}

static always_inline uintptr_t vm_virtual_start(vm_mapping_t *vm) {
  // if the mapping is a stack mapping, vm->address might be above the real start address
  if (vm->flags & VM_STACK) {
    // account for the empty space + the guard page
    size_t empty = vm->virt_size - vm->size;
    return vm->address - empty;
  } else {
    // otherwise the start address is the same as the vm address
    return vm->address;
  }
}

static always_inline interval_t vm_virt_interval(vm_mapping_t *vm) {
  uintptr_t start = vm_virtual_start(vm);
  return intvl(start, start + vm->virt_size);
}

static always_inline interval_t vm_real_interval(vm_mapping_t *vm) {
  uintptr_t start = vm->address;
  return intvl(start, start + vm->size);
}

static always_inline size_t vm_empty_space(vm_mapping_t *vm) {
  size_t size = vm->virt_size - vm->size;
  if (vm->flags & VM_STACK)
    size -= PAGE_SIZE;
  return size;
}

static inline bool vm_are_siblings(vm_mapping_t *a, vm_mapping_t *b) {
  if (a->address > b->address) {
    vm_mapping_t *tmp = a;
    a = b;
    b = tmp;
  }
  if (a == b || a->type != b->type || !(b->flags & VM_SPLIT)) {
    return false;
  }

  vm_mapping_t *curr = LIST_NEXT(a, vm_list);
  while (curr != NULL) {
    if (curr == b) {
      return true;
    }
    curr = LIST_NEXT(curr, vm_list);
  }
  return false;
}

static inline uintptr_t choose_best_hint(uintptr_t hint, uint32_t vm_flags) {
  if (vm_flags & VM_USER) {
    if (hint > 0) {
      return hint;
    }

    if (vm_flags & VM_STACK)
      return HINT_USER_STACK;
    if (vm_flags & VM_MALLOC)
      return HINT_USER_MALLOC;
    return HINT_USER_DEFAULT;
  } else {
    if (hint > KERNEL_SPACE_START && hint < KERNEL_SPACE_END) {
      return hint;
    }

    if (vm_flags & VM_STACK)
      return HINT_KERNEL_STACK;
    if (vm_flags & VM_MALLOC)
      return HINT_KERNEL_MALLOC;
    return HINT_KERNEL_DEFAULT;
  }
}

//
// MARK: Mapping type impls
//

// phys type

static void phys_type_map_internal(vm_mapping_t *vm, uintptr_t phys, size_t size, size_t off) {
  size_t stride = vm_flags_to_size(vm->flags);
  ASSERT(off % stride == 0);
  ASSERT(off + size <= vm->size);

  size_t count = size / stride;
  uintptr_t ptr = vm->address + off;
  uintptr_t phys_ptr = phys + off;
  while (count > 0) {
    page_t *table_pages = NULL;
    recursive_map_entry(ptr, phys_ptr, vm->flags, &table_pages);
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

static void phys_type_unmap_internal(vm_mapping_t *vm, size_t size, size_t off) {
  size_t stride = vm_flags_to_size(vm->flags);
  ASSERT(off % stride == 0);
  ASSERT(off + size <= vm->size);

  size_t count = size / stride;
  uintptr_t ptr = vm->address + off;
  while (count > 0) {
    recursive_unmap_entry(ptr, vm->flags);
    ptr += stride;
    count--;
  }

  cpu_flush_tlb();
}

// pages type

static void page_type_map_internal(vm_mapping_t *vm, page_t *pages, size_t size, size_t off) {
  size_t stride = vm_flags_to_size(vm->flags);
  ASSERT(off % stride == 0);
  ASSERT(off + size <= vm->size);

  size_t count = size / stride;
  uintptr_t ptr = vm->address + off;
  page_t *curr = pages;
  while (curr != NULL) {
    if (count == 0) {
      break;
    }

    struct pte *pte = page_get_mapping(curr, vm);
    if (pte != NULL) {
      // update existing mapping
      pgtable_update_entry_flags(ptr, pte->entry, vm->flags);
    } else {
      // create new mapping
      page_t *table_pages = NULL;
      uint64_t *entry = recursive_map_entry(ptr, curr->address, vm->flags, &table_pages);
      if (table_pages != NULL) {
        page_t *last_page = SLIST_GET_LAST(table_pages, next);
        SLIST_ADD_SLIST(&vm->space->table_pages, table_pages, last_page, next);
      }

      pte = pte_struct_alloc(curr, entry, vm);
      page_add_mapping(curr, pte);
    }

    ptr += stride;
    curr = curr->next;
    count--;
  }

  if (count > 0) {
    panic("not enough pages to map region {:str}\n", &vm->name);
  }
}

static void page_type_unmap_internal(vm_mapping_t *vm, size_t size, size_t off) {
  size_t stride = vm_flags_to_size(vm->flags);
  ASSERT(off % stride == 0);
  ASSERT(off + size <= vm->size);

  uintptr_t ptr = vm->address;
  page_t *curr = vm->vm_pages;
  // get to page at offset
  while (off > 0) {
    if (curr == NULL) {
      panic("page_type_unmap_internal: something went wrong");
    }
    curr = curr->next;
    ptr += stride;
    off -= stride;
  }

  uintptr_t max_ptr = ptr + size;
  while (ptr < max_ptr && curr != NULL) {
    struct pte *pte = page_remove_mapping(curr, vm);
    if (pte != NULL) {
      pgtable_update_entry_flags(ptr, pte->entry, 0);
      pte_struct_free(&pte);
    }
    ptr += stride;
    curr = curr->next;
  }
}

static __ref page_t *page_type_getpage_internal(vm_mapping_t *vm, size_t off) {
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
  return getref(curr);
}

static __ref page_t *page_type_split_internal(__ref page_t *pages, size_t off, __out page_t **tailref) {
  size_t pg_size = pg_flags_to_size(pages->flags);
  ASSERT(pages->flags & PG_HEAD);
  return page_list_split(pages, off/pg_size, tailref);
}

static void page_type_join_internal(__inout page_t **pagesref, __ref page_t *other) {
  page_t *pages = *pagesref;
  if (pages == NULL) {
    *pagesref = moveref(other);
    return;
  }

  ASSERT(pages->flags & PG_HEAD);
  ASSERT(other->flags & PG_HEAD);
  *pagesref = page_list_join(moveref(*pagesref), other);
}

// file type

static vm_file_t *vm_file_fork_internal(vm_file_t *anon, bool shared) {
  todo();
}

struct file_cb_data {
  vm_mapping_t *vm;
  size_t off;
  bool unmap;
};

static void file_map_update_cb(page_t **pageref, size_t off, void *data) {
  page_t *page = *pageref;
  struct file_cb_data *cb = data;
  vm_mapping_t *vm = cb->vm;

  if (cb->unmap) {
    struct pte *pte = page_remove_mapping(page, vm);
    if (pte != NULL) {
      pgtable_update_entry_flags(vm->address + cb->off, pte->entry, 0);
      pte_struct_free(&pte);
    }
  } else {
    struct pte *pte = page_get_mapping(page, vm);
    if (pte == NULL) {
      page_t *table_pages = NULL;
      uint64_t *entry = recursive_map_entry(vm->address + cb->off, page->address, vm->flags, &table_pages);
      if (table_pages != NULL) {
        page_t *last_page = SLIST_GET_LAST(table_pages, next);
        SLIST_ADD_SLIST(&vm->space->table_pages, table_pages, last_page, next);
      }
      page_add_mapping(page, pte_struct_alloc(page, entry, vm));
    } else {
      pgtable_update_entry_flags(vm->address + cb->off, pte->entry, vm->flags);
    }
  }

  cb->off += pg_flags_to_size(page->flags);
}

static void file_type_map_internal(vm_mapping_t *vm, size_t size, size_t off) {
  struct file_cb_data data = {vm, off, /*unmap=*/false};
  vm_file_visit_pages(vm->vm_file, off, vm->size, file_map_update_cb, &data);
}

static void file_type_unmap_internal(vm_mapping_t *vm, size_t size, size_t off) {
  struct file_cb_data data = {vm, off, /*unmap=*/true};
  vm_file_visit_pages(vm->vm_file, off, vm->size, file_map_update_cb, &data);
}

static void file_type_mappage_internal(vm_mapping_t *vm, vm_file_t *file, size_t off, __ref page_t *page) {
  ASSERT((page->flags & PG_HEAD) && (page->head.count == 1));
  ASSERT(pg_flags_to_size(page->flags) == file->pg_size);
  page_t *table_pages = NULL;
  uint64_t *entry = recursive_map_entry(vm->address + off, page->address, vm->flags, &table_pages);
  if (table_pages != NULL) {
    page_t *last_page = SLIST_GET_LAST(table_pages, next);
    SLIST_ADD_SLIST(&vm->space->table_pages, table_pages, last_page, next);
  }
  page_add_mapping(page, pte_struct_alloc(page, entry, vm));
  drop_pages(&page);
}

static vm_file_t *file_type_split_internal(vm_mapping_t *vm, size_t off) {
  return vm_file_split(vm->vm_file, off);
}

static void file_type_join_internal(vm_mapping_t *vm, vm_file_t **otherref) {
  vm_file_merge(vm->vm_file, otherref);
}

//
// MARK: Internal mapping functions
//

static void vm_fork_internal(vm_mapping_t *vm, vm_mapping_t *new_vm) {
  bool shared = new_vm->flags & VM_SHARED;
  switch (vm->type) {
    case VM_TYPE_RSVD:
      break;
    case VM_TYPE_PHYS:
      new_vm->vm_phys = vm->vm_phys;
      break;
    case VM_TYPE_PAGE:
      new_vm->vm_pages = alloc_cow_pages(vm->vm_pages);
      break;
    case VM_TYPE_FILE:
      new_vm->vm_file = vm_file_fork(vm->vm_file);
      break;
    default:
      panic("vm_fork_internal: invalid mapping type");
  }
}

static void vm_update_internal(vm_mapping_t *vm, uint32_t prot) {
  space_lock_assert(vm->space, MA_OWNED);
  prot &= VM_PROT_MASK;

  vm->flags &= ~VM_PROT_MASK;
  vm->flags |= prot;
  if (prot != 0) {
    vm->flags |= VM_MAPPED;
    switch (vm->type) {
      case VM_TYPE_PHYS:
        phys_type_map_internal(vm, vm->vm_phys, vm->size, 0);
        break;
      case VM_TYPE_PAGE:
        page_type_map_internal(vm, vm->vm_pages, vm->size, 0);
        break;
      case VM_TYPE_FILE:
        file_type_map_internal(vm, vm->size, 0);
        break;
      default:
        panic("vm_update_internal: invalid mapping type");
    }
  } else {
    vm->flags &= ~VM_MAPPED;
    switch (vm->type) {
      case VM_TYPE_PHYS:
        phys_type_unmap_internal(vm, vm->size, 0);
        break;
      case VM_TYPE_PAGE:
        page_type_unmap_internal(vm, vm->size, 0);
        break;
      case VM_TYPE_FILE:
        file_type_unmap_internal(vm, vm->size, 0);
        break;
      default:
        panic("vm_update_internal: invalid mapping type");
    }
  }
}

static void vm_split_internal(vm_mapping_t *vm, size_t off, vm_mapping_t *sibling) {
  space_lock_assert(vm->space, MA_OWNED);
  switch (vm->type) {
    case VM_TYPE_PHYS:
      sibling->vm_phys = vm->vm_phys + off;
      break;
    case VM_TYPE_PAGE:
      vm->vm_pages = page_type_split_internal(moveref(vm->vm_pages), off, &sibling->vm_pages);
      break;
    case VM_TYPE_FILE:
      sibling->vm_file = file_type_split_internal(vm, off);
      break;
    default:
      panic("vm_split_internal: invalid mapping type");
  }
}

static void vm_join_internal(vm_mapping_t *vm, vm_mapping_t *other) {
  space_lock_assert(vm->space, MA_OWNED);
  switch (vm->type) {
    case VM_TYPE_PHYS:
      break;
    case VM_TYPE_PAGE:
      page_type_join_internal(&vm->vm_pages, moveref(other->vm_pages));
      break;
    case VM_TYPE_FILE:
      file_type_join_internal(vm, &other->vm_file);
      break;
    default:
      panic("vm_join_internal: invalid mapping type");
  }
}

static void vm_free_internal(vm_mapping_t *vm) {
  space_lock_assert(vm->space, MA_OWNED);
  switch (vm->type) {
    case VM_TYPE_PHYS:
      phys_type_unmap_internal(vm, vm->size, 0);
      vm->vm_phys = 0;
      break;
    case VM_TYPE_PAGE:
      page_type_unmap_internal(vm, vm->size, 0);
      drop_pages(&vm->vm_pages);
      break;
    case VM_TYPE_FILE:
      file_type_unmap_internal(vm, vm->size, 0);
      vm_file_free(&vm->vm_file);
      break;
    default:
      panic("vm_free_internal: invalid mapping type");
  }
}

//
// MARK: Internal virtual space functions
//

static vm_mapping_t *vm_struct_alloc(enum vm_type type, uint32_t vm_flags, uintptr_t vaddr, size_t size, size_t virt_size) {
  vm_mapping_t *vm = kmallocz(sizeof(vm_mapping_t));
  vm->type = type;
  vm->flags = vm_flags;
  vm->address = vaddr;
  vm->size = size;
  vm->virt_size = virt_size;
  return vm;
}

static uintptr_t get_free_region(
  address_space_t *space,
  uintptr_t base,
  size_t size,
  uintptr_t align,
  uint32_t vm_flags,
  vm_mapping_t **closest_vm
) {
  base = do_align(base, align);
  size = do_align(size, align);
  space_lock_assert(space, MA_OWNED);
  if (size > (UINT64_MAX - base) || base + size > space->max_addr) {
    panic("no free address space");
  }

  intvl_node_t *closest_node;
  interval_t intvl = intvl(base, base + size);
  interval_t range = intvl_tree_find_free_gap(space->new_tree, intvl, align, &closest_node);
  if (!closest_node) {
    *closest_vm = NULL;
    return base;
  }

  *closest_vm = closest_node->data;
  return range.start;
}

static bool check_range_free(
  address_space_t *space,
  uintptr_t base,
  size_t size,
  uint32_t vm_flags,
  vm_mapping_t **closest_vm
) {
  space_lock_assert(space, MA_OWNED);
  intvl_node_t *closest_node;
  interval_t intvl = intvl(base, base + size);
  interval_t result = intvl_tree_find_free_gap(space->new_tree, intvl, /*align=*/0, &closest_node);
  if (!intvl_eq(intvl, result)) {
    return false;
  }

  if (closest_node != NULL) {
    *closest_vm = closest_node->data;
  }
  return true;
}

static bool resize_mapping_inplace(vm_mapping_t *vm, size_t new_size) {
  address_space_t *space = vm->space;
  space_lock_assert(space, MA_OWNED);

  interval_t interval = vm_virt_interval(vm);
  intvl_node_t *node = intvl_tree_find(space->new_tree, interval);
  ASSERT(node && node->data == vm);

  // if we are shrinking or growing within the existing empty node virtual space
  // we dont need to update the tree just the mapping size and address. for normal
  // mappings this means just updating vm->size, for stack mappings, we need to bump
  // vm->address up to account for the change.
  off_t delta = diff(new_size, vm->size);
  if (new_size < vm->size) {
    vm->size = new_size;
    if (vm->flags & VM_STACK)
      vm->address += delta;
    return true;
  } else if (new_size > vm->size && new_size <= vm_empty_space(vm)) {
    vm->size = new_size;
    if (vm->flags & VM_STACK)
      vm->address -= delta; // grow down
    return true;
  }

  // for growing beyond the virtual space of the node we need to update the tree
  // but first we need to make sure we dont overlap with the next node
  if (vm->flags & VM_STACK) {
    vm_mapping_t *prev = LIST_PREV(vm, vm_list);
    intvl_node_t *prev_node = intvl_tree_find(space->new_tree, vm_virt_interval(prev));

    // |--prev--| empty space |---vm---|
    size_t empty_space = interval.start - prev_node->interval.end + vm_empty_space(vm);
    if (empty_space < delta) {
      return false;
    }

    intvl_tree_update_interval(space->new_tree, node, -delta, 0);
    vm->address -= new_size - vm->size;
    vm->size = new_size;
  } else {
    vm_mapping_t *next = LIST_NEXT(vm, vm_list);
    intvl_node_t *next_node = intvl_tree_find(space->new_tree, vm_virt_interval(next));

    // |---vm---| empty space |--next--|
    size_t empty_space = next_node->interval.start - interval.end + vm_empty_space(vm);
    if (empty_space < delta) {
      return false;
    }

    intvl_tree_update_interval(space->new_tree, node, 0, delta);
    vm->size = new_size;
  }

  return true;
}

// Splits the vm at the given offset producing a new linked mapping covering
// the range from vm->address+off to the end of the mapping. The new mapping
// is inserted into the space list after the current mapping and returned.
static vm_mapping_t *split_mapping(vm_mapping_t *vm, size_t off) {
  address_space_t *space = vm->space;
  space_lock_assert(space, MA_OWNED);

  ASSERT(off % vm_flags_to_size(vm->flags) == 0);
  interval_t intvl = vm_virt_interval(vm);

  // create new mapping
  vm_mapping_t *new_vm = kmallocz(sizeof(vm_mapping_t));
  new_vm->type = vm->type;
  new_vm->flags = vm->flags | VM_SPLIT;
  new_vm->address = vm->address + off;
  new_vm->size = vm->size - off;
  new_vm->space = space;
  new_vm->name = str_from_cstr(cstr_from_str(vm->name));

  vm_split_internal(vm, off, new_vm);
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

  // resize current interval down and insert new node
  intvl_node_t *node = intvl_tree_find(space->new_tree, intvl);
  off_t delta_end = magnitude(intvl) - off;
  intvl_tree_update_interval(space->new_tree, node, 0, -delta_end);
  intvl_tree_insert(space->new_tree, vm_virt_interval(new_vm), new_vm);
  space->num_mappings++;
  ASSERT(contiguous(vm_virt_interval(vm), vm_virt_interval(new_vm)));

  // insert new node into the list
  LIST_INSERT(&space->mappings, new_vm, vm_list, vm);
  return new_vm;
}

// Joins two formerly split sibling mappings back into a single contiguous one.
// The sibling mapping is removed from the space list and tree and freed, and the
// first (now joined) mapping is returned.
static vm_mapping_t *join_mappings(vm_mapping_t *vm_a, vm_mapping_t *vm_b) {
  address_space_t *space = vm_a->space;
  space_lock_assert(space, MA_OWNED);

  // vm_a and vm_b should both be locked while calling this
  ASSERT((vm_a->flags & VM_LINKED) != 0);
  ASSERT((vm_b->flags & VM_SPLIT) != 0);
  interval_t intvl_a = vm_virt_interval(vm_a);
  interval_t intvl_b = vm_virt_interval(vm_b);

  // remove node_b and update node_a to fill its space
  intvl_node_t *node = intvl_tree_find(space->new_tree, intvl_a);
  intvl_tree_delete(space->new_tree, intvl_b);
  off_t delta_end = (off_t) magnitude(intvl_b);
  intvl_tree_update_interval(space->new_tree, node, 0, delta_end);

  // remove vm_b from the space list
  LIST_REMOVE(&space->mappings, vm_b, vm_list);
  space->num_mappings--;

  vm_join_internal(vm_a, vm_b);
  vm_a->flags &= ~VM_LINKED;
  vm_a->size = vm_a->size + vm_b->size;
  vm_a->virt_size = vm_a->virt_size + vm_b->virt_size;

  str_free(&vm_b->name);
  kfree(vm_b);
  return vm_a;
}

static bool move_mapping(vm_mapping_t *vm, size_t newsize) {
  address_space_t *space = vm->space;
  space_lock_assert(space, MA_OWNED);

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
  intvl_tree_delete(space->new_tree, vm_virt_interval(vm));
  intvl_tree_insert(space->new_tree, intvl(virt_addr, virt_addr + virt_size), vm);

  // switch place of the mapping in the space list
  LIST_REMOVE(&space->mappings, vm, vm_list);
  if (closest->address > virt_addr) {
    closest = LIST_PREV(closest, vm_list);
  }
  // insert into the list
  LIST_INSERT(&space->mappings, vm, vm_list, closest);

  // update the mapping
  vm->address = virt_addr + off;
  vm->size = newsize;
  vm->virt_size = virt_size;
  return true;
}

static void free_mapping(vm_mapping_t **vmp) {
  vm_mapping_t *vm = *vmp;
  address_space_t *space = vm->space;
  space_lock_assert(space, MA_OWNED);

  LIST_REMOVE(&space->mappings, vm, vm_list);
  intvl_tree_delete(space->new_tree, vm_virt_interval(vm));
  space->num_mappings--;

  if (vm->flags & VM_MAPPED) {
    vm_free_internal(vm);
  }
  str_free(&vm->name);
  kfree(vm);
  *vmp = NULL;
}

//
// MARK: vmap_internal
//

static bool are_valid_vmap_args(enum vm_type type, uintptr_t hint, size_t size, size_t vm_size, uint32_t vm_flags, void *data) {
  if (vm_size != 0 && vm_size < size) {
    return false;
  }

  size_t pg_size = PAGE_SIZE;
  if (vm_flags & VM_HUGE_2MB) {
    pg_size = PAGE_SIZE_2MB;
    if (vm_flags & VM_HUGE_1GB) {
      EPRINTF("cant have both 2MB and 1GB huge pages\n");
      return false; // cant have both
    }
  } else if (vm_flags & VM_HUGE_1GB) {
    pg_size = PAGE_SIZE_1GB;
  }

  // check compatibility with alt-sized pages
  if (pg_size != PAGE_SIZE && (vm_flags & VM_STACK)) {
    EPRINTF("cant have huge pages with stack mappings\n");
    return false;
  }

  if (vm_flags & VM_FIXED) {
    // make sure hint is aligned and contained within the right address space
    if (vm_flags & VM_USER && !(hint >= USER_SPACE_START && hint < USER_SPACE_END)) {
      EPRINTF("fixed hint for user mapping is not in user space\n");
      return false;
    }
    if (!(vm_flags & VM_USER) && !(hint >= KERNEL_SPACE_START && hint < KERNEL_SPACE_END)) {
      EPRINTF("fixed hint for kernel mapping is not in kernel space\n");
      return false;
    }
  }

  // make sure the sizes are aligned for given page size
  if (!is_aligned(size, pg_size)) {
    EPRINTF("size is not aligned to page size\n");
    return false;
  } else if (!is_aligned(vm_size, pg_size)) {
    EPRINTF("vm_size is not aligned to page size\n");
    return false;
  }

  switch (type) {
    case VM_TYPE_RSVD:
    case VM_TYPE_PHYS:
      return true;
    case VM_TYPE_PAGE: {
      page_t *page = data;
      if (!(page->flags & PG_HEAD)) {
        EPRINTF("page is not a head page\n");
        return false;
      } else if (size != (page->head.count * pg_size)) {
        EPRINTF("pages do not cover the specified mapping size (%zu != %zu)\n", size, page->head.count * pg_size);
        return false;
      }
      return true;
    }
    case VM_TYPE_FILE: {
      vm_file_t *file = data;
      if (size != file->size) {
        EPRINTF("file size does not match the mapping size (%zu != %zu)\n", size, file->size);
        return false;
      }
      return true;
    }
    default:
      unreachable;
  }
  return true;
}

// creates a new virtual mapping. if the VM_USER flag is set, the mapping will be
// allocated in the provided address space. if the VM_FIXED flag is set, the hint
// address will be used as the base address for the mapping and it will fail if
// the address is not available. by default, the mapping is reflected in the page
// tables of the current address space, but the VM_NOMAP flag can be used to only
// allocate the virtual range. on success a non-zero virtual address is returned.
static int vmap_internal(
  address_space_t *user_space,
  enum vm_type type,
  uintptr_t hint,
  size_t size,
  size_t vm_size,
  uint32_t vm_flags,
  const char *name,
  void *data,
  uintptr_t *out_vaddr
) {
  ASSERT(type < VM_MAX_TYPE);
  if (!are_valid_vmap_args(type, hint, size, vm_size, vm_flags, data)) {
    return -EINVAL;
  }

  // if any protection is given the region must be readable
  if (vm_flags & VM_WRITE || vm_flags & VM_EXEC) {
    vm_flags |= VM_READ;
  }

  size_t pg_size = vm_flags_to_size(vm_flags);
  size_t virt_size = max(vm_size, size);
  size_t virt_off;
  uintptr_t virt_base;
  if (vm_flags & VM_FIXED) {
    if (vm_flags & VM_STACK) {
      // a fixed stack mapping uses the hint as the base address of the
      // mapped part even though the actual virt_base address is lower
      //   ^^higher addresses^^
      //     ...
      //   ======= < vm->address+vm->size
      //    space
      //   ------- < vm->address (hint)
      //    guard
      //   -------
      //    empty
      //   ======= < virt_base
      virt_size += PAGE_SIZE; // guard page
      virt_off = (virt_size - size); // empty space + guard
      if (hint < virt_off) {
        // hint is too low
        return -EINVAL;
      }
      virt_base = hint - virt_off;
    } else {
      //   ^^higher addresses^^
      //     ...
      //   ======= < vm->address+vm->vm_size
      //    empty
      //   ------- < vm->address+vm->size
      //    space
      //   ======= < vm->address (hint) = virt_base
      virt_off = 0;
      virt_base = hint;
    }
  } else {
    // dynamic mappings may adhere to the hint if one is given but it is best-effort
    hint = choose_best_hint(hint, vm_flags);

    if (vm_flags & VM_STACK) {
      virt_size += PAGE_SIZE; // guard page
      virt_off = PAGE_SIZE;
      virt_base = max(hint, virt_size);
    } else {
      virt_off = 0;
      virt_base = hint;
    }
  }

  address_space_t *space = select_space(user_space, virt_base);
  space_lock(space);

  // allocate the virtual space
  int res = 0;
  vm_mapping_t *closest = NULL;
  if (vm_flags & VM_FIXED) {
    // make sure the requested range is free
    if (!check_range_free(space, virt_base, virt_size, vm_flags, &closest)) {
      EPRINTF("requested fixed address range is not free %018p-%018p [name=%s]\n",
              virt_base, virt_base+virt_size, name);
      res = -EADDRNOTAVAIL;
      goto ret;
    }
  } else {
    // dynamically allocated (use virt_base as a starting point)
    virt_base = get_free_region(space, virt_base, virt_size, pg_size, vm_flags, &closest);
    if (virt_base == 0) {
      EPRINTF("failed to satisfy allocation request [name=%s]\n", name);
      res = -ENOMEM;
      goto ret;
    }
  }

  // create the vm_mapping struct and add it to the space
  vm_mapping_t *vm = vm_struct_alloc(type, vm_flags, virt_base+virt_off, size, virt_size);
  vm->name = str_from(name);
  vm->space = space;
  switch (type) {
    case VM_TYPE_RSVD: vm->flags &= ~VM_PROT_MASK; break;
    case VM_TYPE_PHYS: vm->vm_phys = (uintptr_t) data; break;
    case VM_TYPE_PAGE: vm->vm_pages = (page_t *) data; break;
    case VM_TYPE_FILE: vm->vm_file = (vm_file_t *) data; break;
    default: unreachable;
  }

  intvl_tree_insert(space->new_tree, vm_virt_interval(vm), vm);
  space->num_mappings++;

  // add it to the mappings list
  if (closest) {
    LIST_INSERT(&space->mappings, vm, vm_list, closest);
  } else {
    LIST_ADD(&space->mappings, vm, vm_list);
  }

  // map the region if any protection flags are given
  if (vm->flags & VM_PROT_MASK) {
    // unless we're asked to skip it
    if (vm->flags & VM_NOMAP) {
      vm->flags ^= VM_NOMAP; // flag only applied on allocation
      vm->flags |= VM_MAPPED;
    } else {
      vm_update_internal(vm, vm->flags);
    }
  }

  // zero the memory in the region if requested
  if (vm->flags & VM_ZERO) {
    vm->flags ^= VM_ZERO; // flag only applied on allocation
    memset((void *)vm->address, 0, size);
  }

  if (out_vaddr)
    *out_vaddr = virt_base + virt_off;

LABEL(ret);
  space_unlock(space);
  return res;
}

//
// MARK: Public API
//

static always_inline bool can_handle_fault(vm_mapping_t *vm, uintptr_t fault_addr, uint32_t error_code) {
  if (vm->type != VM_TYPE_FILE || !(vm->flags & VM_MAPPED)) {
    return false;
  }

  uint32_t prot = vm->flags & VM_PROT_MASK;
  if (error_code & CPU_PF_W) {
    return prot != 0 && vm->flags & VM_WRITE;
  }
  return prot != 0;
}

__used void page_fault_handler(struct trapframe *frame) {
  uint32_t id = curcpu_id;
  uint64_t fault_addr = __read_cr2();
  if (fault_addr == 0 || !curspace)
    goto exception;

  address_space_t *space = select_space(curspace, fault_addr);
  space_lock(space);

  vm_mapping_t *vm = NULL;
  if (!(frame->error & CPU_PF_P)) {
    // fault was due to a non-present page this might be recoverable
    // check if this fault is related to a vm mapping
    vm = space_get_mapping(space, fault_addr);
    if (vm == NULL || !can_handle_fault(vm, fault_addr, frame->error)) {
      // TODO: support extending stacks automatically if the fault happens
      //       in the guard page
      space_unlock(space);
      goto exception;
    }

    // DPRINTF("non-present page fault in vm_file [vm={:str},addr=%p]\n", &vm->name, fault_addr);
    size_t off = page_trunc(fault_addr - vm->address);
    vm_file_t *file = vm->vm_file;
    page_t *page = vm_file_getpage(file, off);
    if (page == NULL) {
      EPRINTF("failed to get non-present page in vm_file [vm={:str},off=%zu]\n", &vm->name, off);
      space_unlock(space);
      goto exception;
    }

    file_type_mappage_internal(vm, file, off, moveref(page));
    space_unlock(space);
    return; // recover
  }

  // TODO: support COW pages on CPU_PF_W

LABEL(exception);
  kprintf("================== !!! Exception !!! ==================\n");
  kprintf("  Page Fault  - Error: %#b (CPU#%d)\n", (uint32_t)frame->error, curcpu_id);
  kprintf("  CPU#%d  -  RIP: %018p    CR2: %018p\n", id, frame->rip, fault_addr);

  uintptr_t rip = frame->rip - 8;
  uintptr_t rbp = frame->rbp;

  if (frame->error & CPU_PF_U) {
    kprintf("  User mode fault\n");
  } else {
    kprintf("  Kernel mode fault\n");

    char *line_str = debug_addr2line(rip);
    kprintf("  %s\n", line_str);
    kfree(line_str);
    debug_unwind(rip, rbp);
  }

  WHILE_TRUE;
}

//
//

void init_address_space() {
  // the page tables are still pretty much the same as what the bootloader set up for us
  //
  //   0x0000000000000000 - +1Gi           | identity mapped
  //   +1GB - 0x00007FFFFFFFFFFF           | unmapped
  //       ...
  //   === kernel mappings ===
  //   0xFFFF800000000000 - +1Mi           | mapped 0-1Mi
  //   kernel_code_start - kernel_code_end | kernel code (rw)
  //   kernel_code_end - kernel_data_end   | kernel data (rw)
  //       ...
  //   0xFFFFFF8000400000 - +6Mi           | kernel heap (rw)
  //       ...
  //   0xFFFFFF8000C00000 - +rsvd size     | kernel reserved (--)
  //
  init_recursive_pgtable();

  uintptr_t pgtable = get_current_pgtable();
  size_t lowmem_size = kernel_address;
  size_t kernel_code_size = kernel_code_end - kernel_code_start;
  size_t kernel_data_size = kernel_data_end - kernel_code_end;
  size_t reserved_size = kernel_reserved_va_ptr - KERNEL_RESERVED_VA;

  // allocate the shared kernel space
  kernel_space = vm_new_space(KERNEL_SPACE_START, KERNEL_SPACE_END, 0);
  // allocate the default user space
  default_user_space = vm_new_space(USER_SPACE_START, USER_SPACE_END, pgtable);
  set_curspace(default_user_space);

  /////////////////////////////////
  // initial address space layout
  uint32_t kvm_flags = VM_FIXED | VM_NOMAP | VM_MAPPED;
  // we are describing existing mappings, dont remap them
  vmap_rsvd(0, PAGE_SIZE, VM_USER | kvm_flags, "null");
  vmap_phys(0, kernel_virtual_offset, lowmem_size, VM_RDWR | kvm_flags, "lowmem");
  vmap_phys(kernel_address, kernel_code_start, kernel_code_size, VM_RDEXC | kvm_flags, "kernel code");
  vmap_phys(kernel_address + kernel_code_size, kernel_code_end, kernel_data_size, VM_RDWR | kvm_flags, "kernel data");
  vmap_phys(kheap_phys_addr(), KERNEL_HEAP_VA, KERNEL_HEAP_SIZE, VM_RDWR | kvm_flags, "kernel heap");
  vmap_phys(kernel_reserved_start, KERNEL_RESERVED_VA, reserved_size, VM_RDWR | kvm_flags, "kernel reserved");
  /////////////////////////////////

  execute_init_address_space_callbacks();

  // remap boot info struct
  static_assert(sizeof(boot_info_v2_t) <= PAGE_SIZE);
  boot_info_v2 = (void *) vmap_phys((uintptr_t) boot_info_v2, 0, PAGE_SIZE, VM_WRITE, "boot info");

  // fork the default address space but dont deepcopy the user page tables so as
  // to effectively "unmap" the user identity mappings in our new address space.
  // this leaves the original page tables (identity mappings included) for our APs
  space_lock(default_user_space);
  address_space_t *user_space = vm_new_fork_space(default_user_space, /*deepcopy_user=*/false);
  set_current_pgtable(user_space->page_table);
  set_curspace(user_space);
  curproc->space = user_space;
  space_unlock(default_user_space);

  vm_print_address_space();
}

void init_ap_address_space() {
  // do not need to lock default_user_space here because after its creation during init_address_space
  // it is only read from and never written to again
  space_lock(default_user_space);
  address_space_t *user_space = vm_new_fork_space(default_user_space, /*deepcopy_user=*/true);
  set_curspace(user_space);
  curproc->space = user_space;
  space_lock(default_user_space);
}

uintptr_t get_default_ap_pml4() {
  return default_user_space->page_table;
}

//

address_space_t *vm_new_space(uintptr_t min_addr, uintptr_t max_addr, uintptr_t page_table) {
  address_space_t *space = kmallocz(sizeof(address_space_t));
  space->min_addr = min_addr;
  space->max_addr = max_addr;
  space->new_tree = create_intvl_tree();
  space->page_table = page_table;
  mtx_init(&space->lock, MTX_RECURSIVE, "vm_space_lock");
  return space;
}

// the caller must have target space locked
address_space_t *vm_new_fork_space(address_space_t *space, bool deepcopy_user) {
  space_lock_assert(space, MA_OWNED);
  address_space_t *newspace = vm_new_space(space->min_addr, space->max_addr, 0);
  newspace->num_mappings = space->num_mappings;
  ASSERT(space->page_table == get_current_pgtable());

  // fork the page tables
  page_t *meta_pages = NULL;
  // we need to hold a lock on the kernel space during the fork so that
  // none of the kernel entries can change while we're copying them
  space_lock(kernel_space);
  uintptr_t pgtable = fork_page_tables(&meta_pages, deepcopy_user);
  space_unlock(kernel_space);
  newspace->page_table = pgtable;
  SLIST_ADD_SLIST(&newspace->table_pages, meta_pages, SLIST_GET_LAST(meta_pages, next), next);

  // clone and fork all the vm_mappings
  vm_mapping_t *prev_newvm = NULL;
  vm_mapping_t *vm = NULL;
  LIST_FOREACH(vm, &space->mappings, vm_list) {
    vm_mapping_t *newvm = vm_struct_alloc(vm->type, vm->flags, vm->address, vm->size, vm->virt_size);
    newvm->name = str_dup(vm->name);
    newvm->space = newspace;
    vm_fork_internal(vm, newvm);

    // insert into new space
    intvl_tree_insert(newspace->new_tree, vm_virt_interval(newvm), newvm);
    if (prev_newvm) {
      LIST_INSERT(&newspace->mappings, newvm, vm_list, prev_newvm);
    } else {
      LIST_ADD(&newspace->mappings, newvm, vm_list);
    }
    newspace->num_mappings++;
  }
  return newspace;
}

address_space_t *vm_new_empty_space() {
  // fork pages with deepcopy false to allocate a new pml4 with
  // the kernel entries copied over.
  page_t *pml4;
  space_lock(kernel_space);
  fork_page_tables(&pml4, /*deepcopy_user=*/false);
  space_unlock(kernel_space);

  address_space_t *space = kmallocz(sizeof(address_space_t));
  space->min_addr = USER_SPACE_START;
  space->max_addr = USER_SPACE_END;
  space->new_tree = create_intvl_tree();
  space->page_table = pml4->address;
  mtx_init(&space->lock, MTX_RECURSIVE, "vm_space_lock");
  SLIST_ADD(&space->table_pages, pml4, next);
  return space;
}

//
// MARK:- vmap API
//

uintptr_t vmap_rsvd(uintptr_t hint, size_t size, uint32_t vm_flags, const char *name) {
  int res;
  uintptr_t vaddr;
  if ((res = vmap_internal(curspace, VM_TYPE_RSVD, hint, size, size, vm_flags, name, NULL, &vaddr)) < 0) {
    ALLOC_ERROR("vmap: failed to make reserved mapping %s {:err}\n", name, res);
    return 0;
  }
  return vaddr;
}

uintptr_t vmap_phys(uintptr_t phys_addr, uintptr_t hint, size_t size, uint32_t vm_flags, const char *name) {
  int res;
  uintptr_t vaddr;
  if ((res = vmap_internal(curspace, VM_TYPE_PHYS, hint, size, size, vm_flags, name, (void *) phys_addr, &vaddr)) < 0) {
    ALLOC_ERROR("vmap: failed to make physical address mapping %s [phys=%p] {:err}\n", name, phys_addr, res);
    return 0;
  }
  return vaddr;
}

uintptr_t vmap_pages(__ref page_t *pages, uintptr_t hint, size_t size, uint32_t vm_flags, const char *name) {
  int res;
  uintptr_t vaddr;
  if ((res = vmap_internal(curspace, VM_TYPE_PAGE, hint, size, size, vm_flags, name, pages, &vaddr)) < 0) {
    ALLOC_ERROR("vmap: failed to make pages mapping %s [page=%p] {:err}\n", name, pages, res);
    drop_pages(&pages); // release the reference
    return 0;
  }
  return vaddr;
}

uintptr_t vmap_file(vm_file_t *file, uintptr_t hint, size_t vm_size, uint32_t vm_flags, const char *name) {
  int res;
  uintptr_t vaddr;
  if ((res = vmap_internal(curspace, VM_TYPE_FILE, hint, file->size, vm_size, vm_flags, name, file, &vaddr)) < 0) {
    ALLOC_ERROR("vmap: failed to make file mapping %s [file=%p] {:err}\n", name, file, res);
    return 0;
  }
  return vaddr;
}

uintptr_t vmap_anon(size_t vm_size, uintptr_t hint, size_t size, uint32_t vm_flags, const char *name) {
  vm_file_t *file = vm_file_alloc_anon(size, vm_flags_to_size(vm_flags));
  int res;
  uintptr_t vaddr;
  if ((res = vmap_internal(curspace, VM_TYPE_FILE, hint, size, vm_size, vm_flags, name, file, &vaddr)) < 0) {
    ALLOC_ERROR("vmap: failed to make anonymous mapping %s {:err}\n", name, res);
    vm_file_free(&file);
    return 0;
  }
  return vaddr;
}

void *vm_mmap(uintptr_t addr, size_t len, int prot, int flags, int fd, off_t off) {
  uint32_t vm_flags = VM_USER;
  vm_flags |= prot & PROT_READ ? VM_READ : 0;
  vm_flags |= prot & PROT_WRITE ? VM_WRITE : 0;
  vm_flags |= prot & PROT_EXEC ? VM_EXEC : 0;
  vm_flags |= flags & MAP_FIXED ? VM_FIXED : 0;
  if (fd == -1)
    flags |= MAP_ANON;

  if (flags & MAP_ANON) {
    fd = -1;
    off = 0;

    uintptr_t res = vmap_anon(0, addr, len, vm_flags, "mmap anon");
    DPRINTF("mmap: res=%p\n", res);
    if (res == 0)
      return MAP_FAILED;
    return (void *) res;
  }

  vm_file_t *vm_file = fs_get_vmfile(fd, off, len);
  uintptr_t res = vmap_file(vm_file, addr, 0, vm_flags, "mmap file");
  if (res == 0) {
    DPRINTF("failed to map file\n");
    vm_file_free(&vm_file);
    return MAP_FAILED;
  }
  return (void *) res;
}

//

int vmap_free(uintptr_t vaddr, size_t size) {
  // The range [vaddr, vaddr+len-1] may contain one or more non-reserved mappings,
  // but the range must end at a mapping boundary.
  if (!is_valid_range(vaddr, size) || !is_aligned(size, PAGE_SIZE)) {
    return -EINVAL;
  }

  address_space_t *space = select_space(curspace, vaddr);
  space_lock(space);

  int res = 0;
  vm_mapping_t *vm = space_get_mapping(space, vaddr);
  vm_mapping_t *vm_end = space_get_mapping(space, vaddr + size - 1);
  if (vm == NULL || vm_end == NULL) {
    res = -ENOMEM;
    goto ret;
  }

  interval_t i = intvl(vaddr, vaddr + size);
  interval_t i_start = vm_real_interval(vm);
  interval_t i_end = vm_real_interval(vm_end);
  if (i.start < i_start.start || i.end > i_end.end) {
    // the range falls in the virtual mapping range, but some or all of it may
    // be outside the actually mapped region of the vm
    EPRINTF("invalid request: references outside of active region [vaddr=%p, len=%zu]\n", vaddr, size);
    res = -ENOMEM;
    goto ret;
  }

  // make sure that the range starts and ends exactly on the mapping boundaries
  interval_t full = intvl(i_start.start, i_end.end);
  if (!intvl_eq(i, full)) {
    EPRINTF("invalid request: not aligned to mapping boundary [vaddr=%p, len=%zu]\n", vaddr, size);
    return -EINVAL;
  }

  // check that none of the mappings in the range are reserved
  for (vm_mapping_t *curr = vm; curr != LIST_NEXT(vm_end, vm_list); curr = LIST_NEXT(curr, vm_list)) {
    if (curr->type == VM_TYPE_RSVD) {
      EPRINTF("invalid request: attempting to free reserved region [vaddr=%p, len=%zu, start=%p, size=%zu]\n",
              vaddr, size, curr->address, curr->address + curr->size);
      return -EINVAL;
    }
  }

  // free all the mappings
  while (vm != vm_end) {
    vm_mapping_t *next = LIST_NEXT(vm, vm_list);
    free_mapping(&vm);
    vm = next;
  }
  free_mapping(&vm_end);
  vm_end = NULL;

LABEL(ret);
  space_unlock(space);
  return res;
}

int vmap_protect(uintptr_t vaddr, size_t len, uint32_t vm_prot) {
  // Cases for the range [vaddr, vaddr+len-1]
  //   1. part or all of the range is unmapped (or reserved)
  //        - error
  //
  //   2. single mapping with that exact range
  //          |-- mapping --|
  //          ^~~~~prot~~~~~^
  //
  //        - update mapping flags
  //        - call internal functions for mapping to update flags
  //
  //   3. single mapping with a larger range (at start or end of mapping)
  //          |--- mapping ---|  or  |--- mapping ---|
  //          ^~~prot~~^                    ^~~prot~~^
  //
  //        - split the mapping so as to create a linked sibling mapping for the requested range
  //        - update the mapping flags of the new sibling mapping
  //        - call internal functions for sibling mapping to update flags
  //
  //   4. single mapping with a larger range (in middle of mapping)
  //          |--- mapping ---|
  //             ^~~prot~~^
  //
  //        - *same as 3*
  //
  //   5. two or more linked sibling mappings (aligned to the mapping boundaries)
  //          |- rx -|-- ro --|--- rw ---|  or  |-- rw --|-- ro --|
  //          ^~~~~~~~~~~~~~~~~~~~~~~~~~~^      ^~~~~~~~~~~~~~~~~~^
  //
  //        - rejoin the sibling mappings into the first
  //        - update the combined mapping flags
  //        - call internal functions for combined mapping to update flags
  //
  //   6. two or more linked sibling mappings (not aligned to the mapping boundaries)
  //        - error (not supported right now)
  //
  //   7. two or more mixed non-linked mappings
  //        - error
  //
  vm_prot &= VM_PROT_MASK;
  if (!is_valid_range(vaddr, len) || !is_aligned(len, PAGE_SIZE)) {
    return -EINVAL;
  }

  int res = 0;
  address_space_t *space = select_space(curspace, vaddr);
  space_lock(space);

  vm_mapping_t *vm = space_get_mapping(space, vaddr);
  vm_mapping_t *vm_end = space_get_mapping(space, vaddr + len - 1);
  if (vm == NULL || vm_end == NULL || vm->type == VM_TYPE_RSVD || vm_end->type == VM_TYPE_RSVD) {
    res = -ENOMEM;
    goto ret;
  }

  interval_t i_start = vm_real_interval(vm);
  interval_t i_end = vm_real_interval(vm_end);
  interval_t i = intvl(vaddr, vaddr+len);
  bool is_single = vm == vm_end;
  bool are_siblings = vm_are_siblings(vm, vm_end);
  if (!contains_point(i_start, i.start) || !contains_point(i_end, i.end-1)) {
    // case 1
    res = -ENOMEM;
    goto ret;
  } else if (is_single && intvl_eq(i, i_start)) {
    // case 2
    vm_update_internal(vm, vm_prot);
  } else if (is_single && i.start == i_start.start) {
    // case 3
    //   |---vm---|---new_vm---|
    //   ^~update~^
    vm_mapping_t *new_vm = split_mapping(vm, len);
    vm_update_internal(vm, vm_prot);
  } else if (is_single && i.end == i_end.end) {
    // case 3
    //   |---vm---|---new_vm---|
    //            ^~~~update~~~^
    vm_mapping_t *new_vm = split_mapping(vm, i.start - i_start.start);
    vm_update_internal(new_vm, vm_prot);
  } else if (is_single) {
    // case 4
    //   |--vm--|--vm_a--|--vm_b--|
    //          ^~update~^
    vm_mapping_t *vm_a = split_mapping(vm, i.start - i_start.start);
    vm_mapping_t *vm_b = split_mapping(vm_a, len);
    vm_update_internal(vm_a, vm_prot);
  } else if (are_siblings && i.start == i_start.start && i.end == i_end.end) {
    // case 5
    vm_mapping_t *sibling = LIST_NEXT(vm, vm_list);
    while (sibling) {
      vm_mapping_t *next = LIST_NEXT(sibling, vm_list);
      join_mappings(vm, sibling);
      sibling = next;
    }
    vm_update_internal(vm, vm_prot);
  } else if (are_siblings) {
    // case 6
    EPRINTF("error: cannot handle non-aligned sibling mappings [name={:str}]\n", &vm->name);
    res = -ENOMEM;
    goto ret;
  } else {
    // case 7
    EPRINTF("error: cannot update protection of region containing multiple mappings\n");
    res = -ENOMEM;
    goto ret;
  }

LABEL(ret);
  space_unlock(space);
  return res;
}

int vmap_resize(uintptr_t vaddr, size_t old_size, size_t new_size, bool allow_move, uintptr_t *new_vaddr) {
  // The range [vaddr, vaddr+old_size) must represent exactly one mapping of type
  // VM_TYPE_PAGE or VM_TYPE_FILE with a mapping size of old_size. If new_size is
  // less than old_size, the mapping is truncated removing any previously active
  // mappings. If new_size is greater than old_size and allow_move is false, the
  // mapping will be resized in-place if the mapping has non-mapped but claimed
  // vm space, or there is free space after the mapping. If allow_move is true,
  // the mapping will be moved to a new location if the above conditions are not
  // met, and new_vaddr will be set to the new address.
  if (!is_valid_range(vaddr, old_size) || !is_aligned(old_size, PAGE_SIZE) || !is_aligned(new_size, PAGE_SIZE)) {
    return -EINVAL;
  }

  int res = 0;
  address_space_t *space = select_space(curspace, vaddr);
  space_lock(space);

  vm_mapping_t *vm = space_get_mapping(space, vaddr);
  if (vm == NULL || vm->type == VM_TYPE_RSVD) {
    res = -ENOMEM;
    goto ret;
  }

  if ((vm->type != VM_TYPE_PAGE && vm->type != VM_TYPE_FILE) || vm->size != old_size) {
    res = -EINVAL;
    goto ret;
  } else if (vm->flags & VM_LINKED || vm->flags & VM_SPLIT) {
    EPRINTF("cannot resize part of a split mapping [name={:str}]\n", &vm->name);
    res = -EINVAL;
    goto ret;
  }

  if (vm->size == new_size) {
    // nothing to do
    goto ret;
  }

  // first try resizing the existing mapping in place
  uintptr_t old_addr = vm->address;
  if (!resize_mapping_inplace(vm, new_size)) {
    // that didnt work but maybe we can try moving the mapping
    if (allow_move && move_mapping(vm, new_size)) {
      *new_vaddr = vm->address;
    } else {
      res = -ENOMEM;
      goto ret;
    }
  }

  // finally call the appropriate resize function to update the underlying mappings
  if (new_size < old_size) {
    size_t size = old_size - new_size;
    size_t off = new_size;
    if (vm->type == VM_TYPE_PAGE) {
      page_type_unmap_internal(vm, size, off);
    } else if (vm->type == VM_TYPE_FILE) {
      struct file_cb_data data = {vm, off, /*unmap=*/false};
      vm_file_visit_pages(vm->vm_file, off, off + size, file_map_update_cb, &data);
    }
  }

LABEL(ret);
  space_unlock(space);
  return res;
}

//

__ref page_t *vm_getpage(uintptr_t vaddr) {
  if (!is_valid_pointer(vaddr)) {
    return NULL;
  }

  address_space_t *space = select_space(curspace, vaddr);
  space_lock(space);

  page_t *page = NULL;
  vm_mapping_t *vm = space_get_mapping(space, vaddr);
  if (vm == NULL || vm->type == VM_TYPE_RSVD) {
    goto ret;
  }

  size_t off = vaddr - vm->address;
  switch (vm->type) {
    case VM_TYPE_RSVD:
      EPRINTF("invalid request: cannot get page from reserved region\n");
      break;
    case VM_TYPE_PHYS:
      page = alloc_nonowned_pages_at(vm->vm_phys + off, 1, PAGE_SIZE);
      break;
    case VM_TYPE_PAGE:
      page = page_type_getpage_internal(vm, off);
      break;
    case VM_TYPE_FILE:
      page = vm_file_getpage(vm->vm_file, off);
      break;
    default:
      unreachable;
  }

LABEL(ret);
  space_unlock(space);
  return page;
}

__ref page_t *vm_getpage_cow(uintptr_t vaddr) {
  page_t *page = vm_getpage(vaddr);
  if (page == NULL) {
    return NULL;
  }

  ASSERT(!(page->flags & PG_COW));
  page_t *cow_page = alloc_cow_pages(page);
  drop_pages(&page);
  return cow_page;
}

int vm_validate_user_ptr(uintptr_t vaddr, bool write) {
  if (vaddr == 0 || !is_valid_pointer(vaddr)) {
    return -EFAULT;
  }

  int res;
  address_space_t *space = select_space(curspace, vaddr);
  space_lock(space);

  vm_mapping_t *vm = space_get_mapping(space, vaddr);
  if (vm == NULL || vm->type == VM_TYPE_RSVD) {
    res = -EFAULT;
    goto ret;
  }

  if (!(vm->type == VM_TYPE_PAGE || vm->type == VM_TYPE_FILE)) {
    res = -EFAULT;
    goto ret;
  }
  if (write && !(vm->flags & VM_WRITE)) {
    res = -EFAULT;
    goto ret;
  }

  res = !((vm->flags & VM_USER) && (vm->flags & VM_READ));
LABEL(ret);
  space_unlock(space);
  return res;

}

//

uintptr_t vm_virt_to_phys(uintptr_t vaddr) {
  if (!is_valid_pointer(vaddr)) {
    return 0;
  }

  uintptr_t paddr = 0;
  address_space_t *space = select_space(curspace, vaddr);
  space_lock(space);

  vm_mapping_t *vm = space_get_mapping(space, vaddr);
  if (vm == NULL || vm->type == VM_TYPE_RSVD) {
    goto ret;
  }

  size_t off = vaddr - vm->address;
  size_t stride = vm_flags_to_size(vm->flags);
  if (vm->type == VM_TYPE_PHYS) {
    // contiguous physical mapping
    paddr = vm->vm_phys + off;
  } else if (vm->type == VM_TYPE_PAGE) {
    // walk the page list and find the page that contains the address
    page_t *page = vm->vm_pages;
    ASSERT(page->flags & PG_HEAD);
    if (page->head.contiguous) {
      // we can do a simple offset from the head page
      paddr = page->address + off;
      goto ret;
    }

    uintptr_t curr_vaddr = vm->address;
    while (curr_vaddr < vaddr) {
      if (curr_vaddr+stride > vaddr) {
        // the pointer is within this page
        paddr = page->address + (vaddr - curr_vaddr);
        goto ret;
      }

      page = page->next;
      curr_vaddr += stride;
    }
  } else if (vm->type == VM_TYPE_FILE) {
    vm_file_t *file = vm->vm_file;
    paddr = vm_file_getpage_phys(file, off);
  }

LABEL(ret);
  space_unlock(space);
  return paddr;
}

//
// MARK: vmap_other API
//

uintptr_t vmap_other_rsvd(address_space_t *uspace, uintptr_t vaddr, size_t size, uint32_t vm_flags, const char *name) {
  ASSERT(uspace != kernel_space);
  vm_flags |= VM_USER|VM_NOMAP|VM_FIXED;

  int res;
  if ((res = vmap_internal(uspace, VM_TYPE_RSVD, vaddr, size, size, vm_flags, name, NULL, NULL)) < 0) {
    ALLOC_ERROR("vmap: failed to make reserved mapping %s {:err}\n", name, res);
    return 0;
  }
  return vaddr;
}

uintptr_t vmap_other_phys(address_space_t *uspace, uintptr_t paddr, uintptr_t vaddr, size_t size, uint32_t vm_flags, const char *name) {
  size_t pg_size = vm_flags_to_size(vm_flags);
  ASSERT(uspace != kernel_space);
  ASSERT(is_aligned(size, pg_size));


  int res;
  space_lock(uspace);
  vm_flags |= VM_USER|VM_NOMAP|VM_FIXED;
  if ((res = vmap_internal(uspace, VM_TYPE_PHYS, vaddr, size, size, vm_flags, name, (void *)paddr, NULL)) < 0) {
    ALLOC_ERROR("vmap: failed to make phys mapping %s [phys=%p] {:err}\n", name, paddr, res);
    vaddr = 0;
    goto ret;
  }

  page_t *table_pages = NULL;
  nonrecursive_map_frames(uspace->page_table, vaddr, paddr, size/pg_size, vm_flags, &table_pages);
  if (table_pages != NULL) {
    page_t *last_page = SLIST_GET_LAST(table_pages, next);
    SLIST_ADD_SLIST(&uspace->table_pages, table_pages, last_page, next);
  }

LABEL(ret);
  space_unlock(uspace);
  return vaddr;
}

uintptr_t vmap_other_pages(address_space_t *uspace, __ref page_t *pages, uintptr_t hint, size_t size, uint32_t vm_flags, const char *name) {
  ASSERT(uspace != kernel_space);
  DPRINTF("creating pages mapping [vaddr={:p}, size={:d}, flags={:x}, name={:s}]\n",
          hint, size, vm_flags, name);

  int res;
  uintptr_t vaddr;
  space_lock(uspace);
  vm_flags |= VM_USER|VM_NOMAP|VM_FIXED;
  if ((res = vmap_internal(uspace, VM_TYPE_PAGE, hint, size, size, vm_flags, name, pages, &vaddr)) < 0) {
    ALLOC_ERROR("vmap: failed to make pages mapping %s [page=%p] {:err}\n", name, pages, res);
    drop_pages(&pages); // release the reference
    vaddr = 0;
    goto ret;
  }

  page_t *table_pages = NULL;
  nonrecursive_map_pages(uspace->page_table, vaddr, pages, vm_flags, &table_pages);
  if (table_pages != NULL) {
    page_t *last_page = SLIST_GET_LAST(table_pages, next);
    SLIST_ADD_SLIST(&uspace->table_pages, table_pages, last_page, next);
  }

LABEL(ret);
  space_unlock(uspace);
  return vaddr;
}

uintptr_t vmap_other_file(address_space_t *uspace, struct vm_file *file, uintptr_t vaddr, size_t vm_size, uint32_t vm_flags, const char *name) {
  ASSERT(uspace != kernel_space);
  vm_flags |= VM_USER|VM_NOMAP|VM_FIXED;
  DPRINTF("creating file mapping [file={{off=%llu,size=%llu}}, vaddr={:p}, vm_size={:d}, flags={:x}, name={:s}]\n",
          file->off, file->size, vaddr, vm_size, vm_flags, name);

  int res;
  if ((res = vmap_internal(uspace, VM_TYPE_FILE, vaddr, file->size, vm_size, vm_flags, name, file, NULL)) < 0) {
    ALLOC_ERROR("vmap: failed to make file mapping %s {:err}\n", name, res);
    vm_file_free(&file);
  }
  return vaddr;
}

uintptr_t vmap_other_anon(address_space_t *uspace, size_t vm_size, uintptr_t vaddr, size_t size, uint32_t vm_flags, const char *name) {
  ASSERT(uspace != kernel_space);
  vm_flags |= VM_USER|VM_NOMAP|VM_FIXED;
  DPRINTF("creating anonymous mapping [vaddr={:p}, vm_size={:d}, size={:d}, flags={:x}, name={:s}]\n",
          vaddr, vm_size, size, vm_flags, name);

  int res;
  vm_file_t *file = vm_file_alloc_anon(size, vm_flags_to_size(vm_flags));
  if ((res = vmap_internal(uspace, VM_TYPE_FILE, vaddr, size, vm_size, vm_flags, name, file, NULL)) < 0) {
    ALLOC_ERROR("vmap: failed to make anonymous mapping %s {:err}\n", name, res);
    vm_file_free(&file);
  }
  return vaddr;
}


//
// MARK: vm descriptors
//

vm_desc_t *vm_desc_alloc(enum vm_type type, uint64_t address, size_t size, uint32_t vm_flags, const char *name, void *data) {
  vm_desc_t *desc = kmallocz(sizeof(vm_desc_t));
  desc->type = type;
  desc->address = address;
  desc->size = size;
  desc->vm_size = size;
  desc->vm_flags = vm_flags;
  desc->name = name;
  desc->data = data;
  desc->next = NULL;

  if (name == NULL) {
    switch (type) {
      case VM_TYPE_RSVD: desc->name = "rsvd"; break;
      case VM_TYPE_PHYS: desc->name = "phys"; break;
      case VM_TYPE_PAGE: desc->name = "page"; break;
      case VM_TYPE_FILE: desc->name = "file"; break;
      default: unreachable;
    }
  }
  return desc;
}

int vm_desc_map_space(address_space_t *uspace, vm_desc_t *descs) {
  uintptr_t res;
  vm_desc_t *desc = descs;
  while (desc != NULL) {
    uint32_t vm_flags = desc->vm_flags | VM_FIXED;
    switch (desc->type) {
      case VM_TYPE_RSVD:
        DPRINTF("mapping reserved region [name={:str}]\n", desc->name);
        res = vmap_other_rsvd(uspace, desc->address, desc->size, vm_flags, desc->name);
        break;
      case VM_TYPE_PHYS:
        res = vmap_other_phys(uspace, (uintptr_t) desc->data, desc->address, desc->size, vm_flags, desc->name);
        break;
      case VM_TYPE_PAGE:
        res = vmap_other_pages(uspace, moveref(desc->data), desc->address, desc->size, vm_flags, desc->name);
        break;
      case VM_TYPE_FILE:
        res = vmap_other_file(uspace, moveptr(desc->data), desc->address, desc->vm_size, vm_flags, desc->name);
        break;
      default:
        unreachable;
    }

    if (res == 0) {
      EPRINTF("failed to map descriptor in address space [name={:str}]\n", desc->name);
      space_unlock(uspace);
      return -1;
    }
    desc->mapped = true;
    desc = desc->next;
  }
  return 0;
}

void vm_desc_free_all(vm_desc_t **descp) {
  vm_desc_t *desc = *descp;
  while (desc) {
    vm_desc_t *next = desc->next;

    if (desc->type == VM_TYPE_PAGE) {
      drop_pages((page_t **) &desc->data);
    } else if (!desc->mapped && desc->type == VM_TYPE_FILE) {
      vm_file_free((vm_file_t **) &desc->data);
    }

    kfree(desc);
    desc = next;
  }
  *descp = NULL;
}

//
// MARK: vmalloc API
//

void *vmalloc(size_t size, uint32_t vm_flags) {
  ASSERT(!(vm_flags & VM_HUGE_2MB) && !(vm_flags & VM_HUGE_1GB));
  if (size == 0)
    return 0;

  size = align(size, PAGE_SIZE);
  vm_flags &= VM_FLAGS_MASK;
  vm_flags |= VM_MALLOC;
  if (!(vm_flags & VM_PROT_MASK)) {
    return 0; // no protection flags given
  }

  uintptr_t vaddr;
  if (size <= PAGES_TO_SIZE(4)) {
    page_t *pages = alloc_pages(SIZE_TO_PAGES(size));
    if (pages == NULL) {
      ALLOC_ERROR("vmalloc: failed to allocate pages\n");
      return 0;
    }
    vaddr = vmap_pages(moveref(pages), 0, size, vm_flags, "vmalloc");
  } else {
    vaddr = vmap_anon(size, 0, size, vm_flags, "vmalloc");
  }

  if (vaddr == 0) {
    ALLOC_ERROR("vmalloc: failed to make page mapping\n");
    return 0;
  }
  return (void *)vaddr;
}

void vfree(void *ptr) {
  uintptr_t vaddr = (uintptr_t)ptr;
  if (ptr == NULL || !is_valid_pointer(vaddr))
    return;

  address_space_t *space = select_space(curspace, vaddr);
  space_lock(space);

  vm_mapping_t *vm = space_get_mapping(space, vaddr);
  if (vm == NULL) {
    DPANICF("vfree: invalid pointer: {:018p} is not mapped\n", ptr);
  } else if (!((vm->type == VM_TYPE_PAGE || vm->type == VM_TYPE_FILE) && (vm->flags & VM_MALLOC))) {
    DPANICF("vfree: invalid pointer: {:018p} is not a vmalloc pointer\n", ptr);
  } else if (((uintptr_t)ptr) != vm->address) {
    DPANICF("vfree: invalid pointer: {:018p} is not the start of a vmalloc mapping\n", ptr);
  }

  free_mapping(&vm);
  space_unlock(space);
}

//
// MARK: Debug functions
//

static inline const char *prot_to_debug_str(uint32_t vm_flags) {
  if ((vm_flags & VM_PROT_MASK) == 0)
    return "----";

  if (vm_flags & VM_USER) {
    if (vm_flags & VM_WRITE) {
      if (vm_flags & VM_EXEC) {
        return "urwx";
      }
      return "urw-";
    } else if (vm_flags & VM_EXEC) {
      return "ur-x-";
    }
    return "ur--";
  } else {
    if (vm_flags & VM_WRITE) {
      if (vm_flags & VM_EXEC) {
        return "krwx";
      }
      return "krw-";
    } else if (vm_flags & VM_EXEC) {
      return "kr-x-";
    }
    return "kr--";
  }
  return "????";
}

void vm_print_address_space() {
  kprintf("vm: address space mappings\n");
  kprintf("{:$=^80s}\n", " user space ");
  vm_print_mappings(curspace);
  kprintf("{:$=^80s}\n", " kernel space ");
  vm_print_mappings(kernel_space);
  kprintf("{:$=^80}\n");
}

void vm_print_mappings(address_space_t *space) {
  space_lock(space);
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
    vm = LIST_NEXT(vm, vm_list);
  }
  space_unlock(space);
}

void vm_print_address_space_v2() {
  kprintf("vm: address space mappings\n");
  kprintf("{:$=^80s}\n", " user space ");
  vm_print_format_address_space(curspace);
  kprintf("{:$=^80s}\n", " kernel space ");
  vm_print_format_address_space(kernel_space);
  kprintf("{:$=^80}\n");
}

void vm_print_format_address_space(address_space_t *space) {
  space_lock(space);
  vm_mapping_t *prev = NULL;
  vm_mapping_t *vm = LIST_FIRST(&space->mappings);
  uintptr_t prev_end = space->min_addr;
  while (vm) {
    interval_t intvl = vm_virt_interval(vm);
    size_t empty_size = vm_empty_space(vm);
    const char *prot_str = prot_to_debug_str(vm->flags);

    size_t gap_size = intvl.start - prev_end;
    if (gap_size > 0) {
      kprintf("{:^37s} {:$ >10M}\n", "unmapped", gap_size);
    }

    if (vm->flags & VM_STACK) {
      uintptr_t empty_start = intvl.start;
      uintptr_t guard_start = intvl.start+empty_size;

      // in stack mappings the empty space and guard page come first
      if (empty_size > 0) {
        kprintf("{:018p}-{:018p} {:$ >10M}  ----  empty\n", empty_start, empty_start+empty_size, empty_size);
      }

      kprintf("{:018p}-{:018p} {:$ >10M}  ----  guard\n", guard_start, guard_start+PAGE_SIZE, PAGE_SIZE);
      kprintf("{:018p}-{:018p} {:$ >10M}  {:.3s}  {:str}\n",
              vm->address, vm->address+vm->size, vm->size, prot_str, &vm->name);
    } else {
      kprintf("{:018p}-{:018p} {:$ >10M}  {:.4s}  {:str}\n",
                vm->address, vm->address+vm->size, vm->size, prot_str, &vm->name);

      if (empty_size > 0) {
        uintptr_t empty_start = vm->address+vm->size;
        kprintf("{:018p}-{:018p} {:$ >10M}  ----  empty\n", empty_start, empty_start+empty_size, empty_size);
      }
    }

    prev_end = intvl.end;
    vm = LIST_NEXT(vm, vm_list);
  }
  space_unlock(space);
}

//
// MARK: System Calls
//

DEFINE_SYSCALL(mmap, void *, void *addr, size_t len, int prot, int flags, int fd, off_t off) {
  DPRINTF("mmap: addr=%p, len=%zu, prot=%#b, flags=%#x, fd=%d, off=%zu\n", addr, len, prot, flags, fd, off);
  void *res = vm_mmap((uintptr_t) addr, len, prot, flags, fd, off);
  return res;
}

DEFINE_SYSCALL(mprotect, int, void *addr, size_t len, int prot) {
  DPRINTF("mprotect: addr=%p, len=%zu, prot=%d\n", addr, len, prot);
  uint32_t vm_prot = VM_USER;
  if (prot & PROT_READ) {
    vm_prot |= VM_READ;
  } if (prot & PROT_WRITE) {
    vm_prot |= VM_WRITE;
  } if (prot & PROT_EXEC) {
    vm_prot |= VM_EXEC;
  }

  int res = vmap_protect((uintptr_t) addr, len, vm_prot);
  return res;
}

DEFINE_SYSCALL(munmap, int, void *addr, size_t len) {
  DPRINTF("munmap: addr=%p, len=%zu\n", addr, len);
  return vmap_free((uintptr_t) addr, len);
}
