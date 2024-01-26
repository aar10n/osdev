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
#include <kernel/proc.h>
#include <kernel/panic.h>
#include <kernel/string.h>
#include <kernel/printf.h>

#include <interval_tree.h>

#include <abi/mman.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(x, ...) kprintf(x, ##__VA_ARGS__)
#define DPANICF(x, ...) panic(x, ##__VA_ARGS__)
#define ALLOC_ERROR(msg, ...) panic(msg, ##__VA_ARGS__)

// these are the default hints for different combinations of vm flags
// they are used as a starting point for the kernel when searching for
// a free region
#define HINT_USER_DEFAULT   0x0000000040000000ULL // for VM_USER
#define HINT_USER_MALLOC    0x0000040000000000ULL // for VM_USER|VM_MALLOC
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

// generic fault handler that allocates and returns a new page
static __move page_t *vm_fault_alloc_page(vm_mapping_t *vm, size_t off, uint32_t vm_flags, void *data) {
  vm_anon_t *anon = data;
  if (off >= vm->size) {
    return NULL;
  }

  // DPRINTF("vm_fault_alloc_page: vm={:str} off=%zu vm_flags=%x\n", &vm->name, off, vm_flags);
  page_t *page = alloc_pages_size(1, vm_flags_to_size(vm_flags));
  if (page == NULL) {
    DPRINTF("vm_fault_alloc_page: failed to allocate page\n");
    return NULL;
  }
  return page;
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
  return "???";
}

static always_inline bool space_contains_addr(address_space_t *space, uintptr_t addr) {
  return addr >= space->min_addr && addr < space->max_addr;
}

static always_inline bool is_valid_pointer(uintptr_t ptr) {
  return ptr >= USER_SPACE_START && ptr < USER_SPACE_END &&
         ptr >= KERNEL_SPACE_START && ptr < KERNEL_SPACE_END;
}

static always_inline bool is_valid_range(uintptr_t start, size_t len) {
  if (start <= USER_SPACE_START) {
    return start + len <= USER_SPACE_END;
  } else if (start >= KERNEL_SPACE_START) {
    return start + len <= KERNEL_SPACE_END;
  }
  return false;
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

static inline uintptr_t choose_best_hint(address_space_t *space, uintptr_t hint, uint32_t vm_flags) {
  if (hint != 0) {
    if (space_contains_addr(space, hint)) {
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

static inline void *array_alloc(size_t count, size_t size) {
  size_t total = count * size;
  void *ptr;
  if (total >= PAGE_SIZE) {
    ptr = vmalloc(total, VM_RDWR);
    memset(ptr, 0, align(total, PAGE_SIZE));
  } else {
    ptr = kmallocz(total);
  }
  if (ptr == NULL)
    panic("array_alloc: failed to allocate %zu bytes\n", total);


  return ptr;
}

static inline void *array_realloc(void *ptr, size_t old_count, size_t new_count, size_t size) {
  size_t old_total = old_count * size;
  size_t new_total = new_count * size;
  if (old_total >= PAGE_SIZE && new_total >= PAGE_SIZE &&
      SIZE_TO_PAGES(old_total) == SIZE_TO_PAGES(new_total)) {
    // no need to reallocate
    return ptr;
  }

  void *new_ptr;
  if (new_total >= PAGE_SIZE) {
    new_ptr = vmalloc(new_total, VM_RDWR);
    memset(new_ptr + old_total, 0, align(new_total, PAGE_SIZE) - old_total);
  } else {
    new_ptr = kmalloc(new_total);
    memset(new_ptr + old_total, 0, new_total - old_total);
  }
  if (new_ptr == NULL)
    panic("array_realloc: failed to allocate %zu bytes\n", new_total);

  memcpy(new_ptr, ptr, old_total);
  if (old_total >= PAGE_SIZE) {
    vfree(ptr);
  } else {
    kfree(ptr);
  }
  return new_ptr;
}

static inline void array_free(void *ptr, size_t count, size_t size) {
  size_t total = count * size;
  if (total >= PAGE_SIZE) {
    vfree(ptr);
  } else {
    kfree(ptr);
  }
}

static vm_anon_t *anon_struct_alloc(vm_anon_t *anon, size_t size, size_t pgsize) {
  if (anon == NULL) {
    anon = kmallocz(sizeof(vm_anon_t));
    anon->pg_size = pgsize;
    anon->get_page = vm_fault_alloc_page;
    anon->data = anon;
  }

  if (anon->pg_size != pgsize) {
    panic("anon_struct_alloc: page size mismatch");
  }

  if (anon->pages == NULL && size == 0) {
    return anon;
  }

  size_t new_length = size / pgsize;
  size_t new_capacity = next_pow2(new_length);
  // DPRINTF("anon_struct_alloc: size=%zu pgsize=%zu new_length=%zu new_capacity=%zu\n",
  //         size, pgsize, new_length, new_capacity);

  if (anon->pages == NULL) {
    // allocate new
    anon->pages = array_alloc(new_capacity, sizeof(page_t *));
    anon->capacity = new_capacity;
    anon->length = new_length;
  } else if (new_length > anon->capacity) {
    anon->pages = array_realloc(anon->pages, anon->capacity, new_capacity, sizeof(page_t *));
    anon->capacity = new_capacity;
    anon->length = new_length;
  } else if (new_length < anon->length) {
    // only reallocate if the difference is > 1/4 of the current length
    if (anon->length - new_length > anon->length / 4) {
      // free any pages in the range that will be removed
      for (size_t i = new_length; i < anon->length; i++) {
        if (anon->pages[i] != NULL) {
          drop_pages(&anon->pages[i]);
        }
      }

      anon->pages = array_realloc(anon->pages, anon->capacity, new_capacity, sizeof(page_t *));
      anon->capacity = new_capacity;
      anon->length = new_length;
    }
  } else {
    // no need to reallocate, just update the length
    anon->length = new_length;
  }
  return anon;
}

static vm_anon_t *anon_struct_alloc_len(vm_anon_t *anon, size_t length, size_t pgsize) {
  return anon_struct_alloc(anon, length * pgsize, pgsize);
}

static void anon_struct_free(vm_anon_t *anon) {
  if (anon->pages != NULL) {
    // free the pages and array
    for (size_t i = 0; i < anon->length; i++) {
      if (anon->pages[i] != NULL) {
        drop_pages(&anon->pages[i]);
      }
    }

    array_free(anon->pages, anon->capacity, sizeof(page_t *));
    anon->pages = NULL;
  }
  kfree(anon);
}

static int anon_struct_addpage(vm_anon_t *anon, size_t index, __move page_t *page) {
  ASSERT(page->flags & PG_HEAD && page->head.count == 1);
  ASSERT(pg_flags_to_size(page->flags) == anon->pg_size);

  size_t max_size = (index + 1) * anon->pg_size;
  if (max_size > anon->length * anon->pg_size) {
    anon_struct_alloc(anon, max_size, anon->pg_size);
  }

  if (anon->pages[index] != NULL) {
    panic("anon_struct_addpages: page already mapped at offset %zu", index * anon->pg_size);
  }
  anon->pages[index] = moveref(page);
  return 0;
}

static inline __move page_t *anon_struct_getpage(vm_anon_t *anon, size_t index) {
  if (index >= anon->length) {
    return NULL;
  }
  return getref(anon->pages[index]);
}

static inline uintptr_t anon_struct_get_phys(vm_anon_t *anon, size_t index) {
  if (index >= anon->length) {
    return 0;
  }
  return anon->pages[index]->address;
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
      if (curr->mapping == NULL) {
        // memory leak if these are unmapped pages that are not needed
        panic("more pages than needed to map region {:str}", &vm->name);
      }
      break;
    }

    // the page must be owned by the mapping if updating
    if (curr->mapping == NULL) {
      // mapping for the first time
      curr->mapping = vm;
    } else {
      // updating existing mappings
      ASSERT(curr->mapping == vm);
    }

    page_t *table_pages = NULL;
    recursive_map_entry(ptr, curr->address, vm->flags, &table_pages);
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

static void page_type_unmap_internal(vm_mapping_t *vm, size_t size, size_t off) {
  size_t stride = vm_flags_to_size(vm->flags);
  ASSERT(off % stride == 0);
  ASSERT(off + size <= vm->size);

  uintptr_t ptr = vm->address;
  page_t *curr = vm->vm_pages;
  while (off > 0) {
    if (curr == NULL) {
      panic("page_type_unmap_internal: something went wrong");
    }
    // get to page at offset
    curr = curr->next;
    ptr += stride;
    off -= stride;
  }

  uintptr_t max_ptr = ptr + size;
  while (ptr < max_ptr && curr != NULL) {
    ASSERT(curr->mapping != NULL);
    recursive_unmap_entry(ptr, vm->flags);
    ptr += stride;

    ASSERT(curr->mapping == vm);
    curr->mapping = NULL;
    curr = curr->next;
  }

  cpu_flush_tlb();
}

static __move page_t *page_type_getpage_internal(vm_mapping_t *vm, size_t off) {
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

static __move page_t *page_type_split_internal(__move page_t **pagesref, size_t off) {
  page_t *pages = *pagesref;
  size_t pg_size = pg_flags_to_size(pages->flags);
  ASSERT(pages->flags & PG_HEAD);
  return page_list_split(pagesref, off/pg_size);
}

static void page_type_join_internal(__move page_t **pagesref, __move page_t *other) {
  page_t *pages = *pagesref;
  if (pages == NULL) {
    *pagesref = moveref(other);
    return;
  }

  ASSERT(pages->flags & PG_HEAD);
  ASSERT(other->flags & PG_HEAD);
  ASSERT(pages->head.contiguous && other->head.contiguous);
  ASSERT(pages->head.count + other->head.count == pages->head.count);

  page_t *curr = SLIST_GET_LAST(pages, next);
  curr->next = getref(other);
  other->flags &= ~PG_HEAD;
  pages->head.count += other->head.count;
}

// anon type

static vm_anon_t *anon_type_fork_internal(vm_anon_t *anon) {
  vm_anon_t *new_anon = anon_struct_alloc_len(NULL, anon->length, anon->pg_size);
  for (size_t i = 0; i < anon->length; i++) {
    page_t *page = anon->pages[i];
    if (page == NULL) {
      continue;
    }
    new_anon->pages[i] = alloc_cow_pages(anon->pages[i]);
  }
  return new_anon;
}

static void anon_type_map_internal(vm_mapping_t *vm, vm_anon_t *anon, size_t size, size_t off) {
  size_t stride = vm_flags_to_size(vm->flags);
  ASSERT(off % stride == 0);
  ASSERT(off + size <= vm->size);

  if (anon->pages == NULL) {
    return;
  }

  size_t count = size / stride;
  size_t ioff = off / stride;
  uintptr_t ptr = vm->address + off;
  for (size_t i = 0; i < count; i++) {
    if (ioff+i >= anon->length)
      break;

    page_t *page = anon->pages[ioff + i];
    if (page == NULL)
      continue; // ignore holes

    page_t *table_pages = NULL;
    recursive_map_entry(ptr, page->address, vm->flags, &table_pages);
    ptr += stride;

    if (page->mapping == NULL) {
      // mapping for the first time
      page->mapping = vm;
    } else {
      // updating existing mappings
      ASSERT(page->mapping == vm);
    }

    if (table_pages != NULL) {
      page_t *last_page = SLIST_GET_LAST(table_pages, next);
      SLIST_ADD_SLIST(&vm->space->table_pages, table_pages, last_page, next);
    }
  }

  cpu_flush_tlb();
}

static void anon_type_unmap_internal(vm_mapping_t *vm, size_t size, size_t off) {
  ASSERT(vm->type == VM_TYPE_ANON);
  vm_anon_t *anon = vm->vm_anon;
  size_t stride = vm_flags_to_size(vm->flags);
  ASSERT(off + size <= vm->size);

  uintptr_t ptr = vm->address;
  size_t start_index = off / stride;
  size_t max_index = (off + size) / stride;
  for (size_t i = start_index; i < max_index; i++) {
    if (i >= anon->length)
      break;

    page_t *page = anon->pages[i];
    if (page != NULL) {
      recursive_unmap_entry(ptr, page->flags);

      ASSERT(page->mapping != NULL);
      page->mapping = NULL;
      anon->mapped--;
    }
    ptr += stride;
  }

  cpu_flush_tlb();
}

static __move page_t *anon_type_getpage_internal(vm_mapping_t *vm, size_t off) {
  ASSERT(vm->type == VM_TYPE_ANON);
  vm_anon_t *anon = vm->vm_anon;
  size_t stride = vm_flags_to_size(vm->flags);
  ASSERT(off <= vm->size);

  size_t index = off / stride;
  ASSERT(index < anon->length);
  return getref(anon->pages[index]);
}

static void anon_type_putpages_internal(vm_mapping_t *vm, vm_anon_t *anon, size_t size, size_t off, __move page_t *pages) {
  size_t stride = vm_flags_to_size(vm->flags);
  ASSERT(off % stride == 0);
  ASSERT(off + size <= vm->size);
  if (pages == NULL) {
    return;
  }

  size_t index = off / stride;
  uintptr_t ptr = vm->address + off;
  while (pages != NULL) {
    if (anon_struct_getpage(anon, index) != NULL) {
      panic("anon_putpage_internal: page already mapped at offset %zu [vm={:str}]", index * stride, &vm->name);
    }

    page_t *curr = page_list_split(&pages, 1);
    page_t *table_pages = NULL;
    if (pg_flags_to_size(curr->flags) != stride) {
      panic("anon_putpage_internal: page size does not match vm page size");
    }

    recursive_map_entry(ptr, curr->address, vm->flags, &table_pages);
    ASSERT(curr->mapping == NULL);
    curr->mapping = vm;
    ptr += stride;

    anon_struct_addpage(anon, index, moveref(curr));
    anon->mapped++;
    index++;

    if (table_pages != NULL) {
      page_t *last_page = SLIST_GET_LAST(table_pages, next);
      SLIST_ADD_SLIST(&vm->space->table_pages, table_pages, last_page, next);
    }
  }

  cpu_flush_tlb();
}

static vm_anon_t *anon_type_split_internal(vm_anon_t *anon, size_t off, vm_mapping_t *other_vm) {
  size_t stride = anon->pg_size;
  ASSERT(off % stride == 0);

  size_t index = off / stride;
  size_t new_length = anon->length - index;

  vm_anon_t *new_anon = anon_struct_alloc_len(NULL, new_length, stride);
  for (size_t i = index; i < anon->length; i++) {
    page_t *page = moveref(anon->pages[i]);
    if (page == NULL) {
      continue;
    }

    // move from old to new
    if (page->mapping != NULL) {
      // only update the mapped counts if it actually had been
      page->mapping = other_vm;
      anon->mapped--;
      new_anon->mapped++;
    }
    new_anon->pages[i-index] = moveref(page);
  }
  return new_anon;
}

static void anon_type_join_internal(vm_anon_t *anon, vm_anon_t *other, vm_mapping_t *original_vm) {
  size_t stride = anon->pg_size;
  size_t old_length = anon->length;
  // make sure anon array is big enough for the joined size
  anon = anon_struct_alloc_len(anon, old_length+other->length, anon->pg_size);
  // move over the pages
  size_t base_index = original_vm->size/stride;
  for (size_t i = 0; i < other->length; i++) {
    page_t *page = moveref(other->pages[i]);
    if (page == NULL) {
      continue;
    }

    if (page->mapping != NULL) {
      page->mapping = original_vm;
      other->mapped--;
      anon->mapped++;
    }
    anon->pages[base_index+i] = moveref(page);
  }

  anon_struct_free(other);
}

// MARK: Internal mapping functions

static void vm_fork_internal(vm_mapping_t *vm, vm_mapping_t *new_vm) {
  switch (vm->type) {
    case VM_TYPE_RSVD:
      break;
    case VM_TYPE_PHYS:
      new_vm->vm_phys = vm->vm_phys;
      break;
    case VM_TYPE_PAGE:
      new_vm->vm_pages = alloc_cow_pages(vm->vm_pages);
      break;
    case VM_TYPE_ANON:
      new_vm->vm_anon = anon_type_fork_internal(vm->vm_anon);
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
      case VM_TYPE_ANON:
        anon_type_map_internal(vm, vm->vm_anon, vm->size, 0);
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
      case VM_TYPE_ANON:
        anon_type_unmap_internal(vm, vm->size, 0);
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
      break;
    case VM_TYPE_PAGE:
      sibling->vm_pages = page_type_split_internal(&vm->vm_pages, off);
      break;
    case VM_TYPE_ANON:
      sibling->vm_anon = anon_type_split_internal(vm->vm_anon, off, sibling);
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
    case VM_TYPE_ANON:
      anon_type_join_internal(vm->vm_anon, other->vm_anon, vm);
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
    case VM_TYPE_ANON:
      anon_type_unmap_internal(vm, vm->size, 0);
      anon_struct_free(vm->vm_anon);
      vm->vm_anon = NULL;
      break;
    default:
      panic("vm_free_internal: invalid mapping type");
  }
}

// MARK: Virtual space allocation

static vm_mapping_t *vm_struct_alloc(enum vm_type type, uint32_t vm_flags, size_t size, size_t virt_size) {
  vm_mapping_t *vm = kmallocz(sizeof(vm_mapping_t));
  vm->type = type;
  vm->flags = vm_flags;
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
  space_lock_assert(space, MA_OWNED);
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
    interval_t i = vm_virt_interval(curr);
    interval_t j = prev ? vm_virt_interval(prev) : i;

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
      curr = LIST_PREV(curr, vm_list);
    } else {
      // go forward looking for a free space from the bottom of each free region
      bool contig = contiguous(i, j);
      if (!contig && i.start > addr && i.start - addr >= size)
        break;

      addr = align(i.end, align);
      prev = curr;
      curr = LIST_NEXT(curr, vm_list);
    }
  }

  if (size > (UINT64_MAX - addr) || addr + size > space->max_addr) {
    panic("no free address space");
  }

  *closest_vm = prev;
  return addr;
}

static bool check_range_free(
  address_space_t *space,
  uintptr_t base,
  size_t size,
  uint32_t vm_flags,
  vm_mapping_t **closest_vm
) {
  space_lock_assert(space, MA_OWNED);
  interval_t interval = intvl(base, base + size);
  intvl_node_t *closest = intvl_tree_find_closest(space->new_tree, interval);
  if (closest == NULL) {
    return true;
  }

  if (!overlaps(interval, closest->interval)) {
    *closest_vm = closest->data;
    return true;
  }
  return false;
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
  new_vm->name = str_copy_cstr(cstr_from_str(vm->name));

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
// MARK: Public API
//

static always_inline bool can_handle_fault(vm_mapping_t *vm, uintptr_t fault_addr, uint32_t error_code) {
  if (vm->type != VM_TYPE_ANON || !(vm->flags & VM_MAPPED)) {
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

    // DPRINTF("non-present page fault in vm_anon [vm={:str},addr=%p]\n", &vm->name, fault_addr);
    size_t off = align_down(fault_addr - vm->address, PAGE_SIZE);
    vm_anon_t *anon = vm->vm_anon;
    page_t *page = anon->get_page(vm, off, vm->flags, anon->data);
    if (page == NULL) {
      DPRINTF("failed to get non-present page in vm_file [vm={:str},off=%zu]\n", &vm->name, off);
      space_unlock(space);
      goto exception;
    }

    // map the new page into the file
    size_t size = vm_flags_to_size(vm->flags);
    anon_type_putpages_internal(vm, anon, size, off, page);

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
  address_space_t *user_space = vm_fork_space(default_user_space, /*deepcopy_user=*/false);
  set_current_pgtable(user_space->page_table);
  set_curspace(user_space);

  vm_print_address_space();
}

void init_ap_address_space() {
  // do not need to lock default_user_space here because after its creation during init_address_space
  // it is only read from and never written to again
  address_space_t *user_space = vm_fork_space(default_user_space, true);
  set_curspace(user_space);
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

address_space_t *vm_new_uspace() {
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

// the caller must have target space locked
address_space_t *vm_fork_space(address_space_t *space, bool deepcopy_user) {
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
    vm_mapping_t *newvm = vm_struct_alloc(vm->type, vm->flags, vm->size, vm->virt_size);
    newvm->name = str_dup(vm->name);
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

//
//

size_t rw_unmapped_pages(__ref page_t *pages, size_t off, kio_t *kio) {
  size_t pgsize = pg_flags_to_size(pages->flags);
  ASSERT(pgsize == PAGE_SIZE);
  ASSERT(pages->flags & PG_HEAD);

  // get start page for offset
  page_t *page = pages;
  while (page && off >= PAGE_SIZE) {
    off -= pgsize;
    page = page->next;
  }

  size_t n = 0;
  size_t remain;
  while ((remain = kio_remaining(kio)) > 0) {
    if (page == NULL) {
      break;
    }

    n += rw_unmapped_page(page, off, kio);
    off = 0;
    page = page->next;
  }
  return n;
}

void fill_unmapped_pages(__ref page_t *pages, uint8_t v) {
  page_t *page = pages;
  while (page) {
    fill_unmapped_page(page, v);
    page = page->next;
  }
}

//
// MARK: Vmap API
//

// creates a new virtual mapping. if the VM_USER flag is set, the mapping will be
// allocated in the provided address space. if the VM_FIXED flag is set, the hint
// address will be used as the base address for the mapping and it will fail if
// the address is not available. by default, the mapping is reflected in the page
// tables of the current address space, but the VM_NOMAP flag can be used to only
// allocate the virtual range. on success a non-zero virtual address is returned.
static uintptr_t vmap_internal(
  address_space_t *user_space,
  enum vm_type type,
  uintptr_t hint,
  size_t size,
  size_t vm_size,
  uint32_t vm_flags,
  const char *name,
  void *arg
) {
  ASSERT(type < VM_MAX_TYPE);
  vm_size = max(vm_size, size);
  if (!is_valid_pointer(hint) || vm_size == 0) {
    return 0;
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
    if (!(vm_flags & VM_USER))
      DPRINTF("hint %p is not aligned to page size %zu [name=%s]\n", hint, pgsize, name);
    return 0;
  }

  vm_mapping_t *vm = vm_struct_alloc(type, vm_flags, size, vm_size);
  size_t off = 0;
  if (vm_flags & VM_STACK) {
    // stack mappings grow down and have a guard page below the stack. we also
    // positition the mapping such that the empty virtual space is below it so
    // it can grow down into the free space if needed. note that vm->address
    // will point to the bottom of the stack.
    //     ...
    //   ======= < mapping end
    //    stack
    //   ------- < vm->address
    //    guard
    //   -------
    //    empty
    //   ======= < mapping start
    vm->virt_size += PAGE_SIZE;
    off = (vm->virt_size - vm->size); // offset vm->address
  } else {
    // non-stack mappings are not offset at all and the empty space comes after
    //     ...
    //   ======= < mapping end
    //    empty
    //   -------
    //    pages
    //   ======= < vm->address (mapping start)
  }

  address_space_t *space;
  if (vm_flags & VM_USER) {
    space = user_space;
  } else {
    space = kernel_space;
  }

  // allocate the virtual address range for the mapping
  space_lock(space);
  uintptr_t virt_addr = 0;
  vm_mapping_t *closest = NULL;
  if (vm_flags & VM_FIXED) {
    if (!space_contains_addr(space, hint)) {
      if (!(vm_flags & VM_USER)) { // panic for kernel requests
        panic("vmap: hint address not in address space: %p [name=%s]\n", hint, name);
      }
      goto error;
    }

    if (vm_flags & VM_STACK) {
      if (hint < vm->virt_size) {
        if (!(vm_flags & VM_USER)) {
          panic("vmap: hint address is too low for requested stack size [name=%s]\n", name);
        }
        goto error;
      }
      hint -= vm->virt_size;
    }
    virt_addr = hint;

    // make sure the requested range is free
    if (!check_range_free(space, hint, vm->virt_size, vm_flags, &closest)) {
      if (!(vm_flags & VM_USER))
        DPRINTF("vmap: requested fixed address range is not free %p-%p [name=%s]\n", hint, hint + vm->virt_size, name);
      goto error;
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
      DPRINTF("vmap: failed to satisfy allocation request [name=%s]\n", name);
      goto error;
    }
  }

  vm->address = virt_addr + off;
  vm->name = str_from(name);
  vm->space = space;

  switch (vm->type) {
    case VM_TYPE_RSVD: vm->flags &= ~VM_PROT_MASK; break;
    case VM_TYPE_PHYS: vm->vm_phys = (uintptr_t) arg; break;
    case VM_TYPE_PAGE: vm->vm_pages = (page_t *) arg; break;
    case VM_TYPE_ANON: vm->vm_anon = (vm_anon_t *) arg; break;
    default:
      unreachable;
  }

  // insert mapping into to the mappings list
  if (closest) {
    if (closest->address > virt_addr) {
      // we dont care about closeness here we just want the mapping
      // immediately before where the new mapping is going to be
      closest = LIST_PREV(closest, vm_list);
    }

    // insert into the list
    LIST_INSERT(&space->mappings, vm, vm_list, closest);
  } else {
    // first mapping
    LIST_ADD(&space->mappings, vm, vm_list);
  }

  // insert mapping to address space tree
  intvl_tree_insert(space->new_tree, vm_virt_interval(vm), vm);
  space->num_mappings++;

  // map the region if any protection flags are given
  if (vm->flags & VM_PROT_MASK) {
    // unless we're asked to skip it
    if (vm->flags & VM_NOMAP) {
      vm->flags ^= VM_NOMAP; // flag only applied on allocation
    } else {
      vm_update_internal(vm, vm->flags);
    }
  }
  space_unlock(space);
  return virt_addr + off;

LABEL(error);
  space_unlock(space);
  kfree(vm);
  return 0;
}

// these functions dont need any locks held

int vmap_rsvd(uintptr_t hint, size_t size, uint32_t vm_flags, const char *name) {
  uintptr_t vaddr = vmap_internal(curspace, VM_TYPE_RSVD, hint, size, size, vm_flags, name, NULL);
  if (vaddr == 0) {
    ALLOC_ERROR("vmap: failed to make reserved mapping %s\n", name);
    return -1;
  }
  return 0;
}

uintptr_t vmap_phys(uintptr_t phys_addr, uintptr_t hint, size_t size, uint32_t vm_flags, const char *name) {
  uintptr_t vaddr = vmap_internal(curspace, VM_TYPE_PHYS, hint, size, size, vm_flags, name, (void *) phys_addr);
  if (vaddr == 0) {
    ALLOC_ERROR("vmap: failed to make physical address mapping %s [phys=%p]\n", name, phys_addr);
    return 0;
  }
  return vaddr;
}

uintptr_t vmap_pages(__move page_t *pages, uintptr_t hint, size_t size, uint32_t vm_flags, const char *name) {
  ASSERT(pages->flags & PG_HEAD);
  if (VM_HUGE_2MB & vm_flags) {
    ASSERT(pages->flags & PG_BIGPAGE);
  } else if (VM_HUGE_1GB & vm_flags) {
    ASSERT(pages->flags & PG_HUGEPAGE);
  }

  uintptr_t vaddr = vmap_internal(curspace, VM_TYPE_PAGE, hint, size, size, vm_flags, name, pages);
  if (vaddr == 0) {
    ALLOC_ERROR("vmap: failed to make pages mapping %s [page=%p]\n", name, pages);
    drop_pages(&pages); // release the reference
    return 0;
  }
  return vaddr;
}

uintptr_t vmap_anon(size_t vm_size, uintptr_t hint, size_t size, uint32_t vm_flags, const char *name) {
  vm_anon_t *anon = anon_struct_alloc(NULL, size, vm_flags_to_size(vm_flags));
  uintptr_t vaddr = vmap_internal(curspace, VM_TYPE_ANON, hint, size, vm_size, vm_flags, name, anon);
  if (vaddr == 0) {
    ALLOC_ERROR("vmap: failed to make anonymous mapping %s\n", name);
    anon_struct_free(anon);
    return 0;
  }
  return vaddr;
}

int vm_free(uintptr_t vaddr, size_t size) {
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
    DPRINTF("vm_free: invalid request: references outside of active region [vaddr=%p, len=%zu]\n", vaddr, size);
    res = -ENOMEM;
    goto ret;
  }

  // make sure that the range starts and ends exactly on the mapping boundaries
  interval_t full = intvl(i_start.start, i_end.end);
  if (!intvl_eq(i, full)) {
    DPRINTF("vm_free: invalid request: not aligned to mapping boundary [vaddr=%p, len=%zu]\n", vaddr, size);
    return -EINVAL;
  }

  // check that none of the mappings in the range are reserved
  for (vm_mapping_t *curr = vm; curr != LIST_NEXT(vm_end, vm_list); curr = LIST_NEXT(curr, vm_list)) {
    if (curr->type == VM_TYPE_RSVD) {
      DPRINTF("vm_free: invalid request: attempting to free reserved region [vaddr=%p, len=%zu, start=%p, size=%zu]\n",
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

int vm_protect(uintptr_t vaddr, size_t len, uint32_t prot) {
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
  prot &= VM_PROT_MASK;
  if (!contains_point(i_start, i.start) || !contains_point(i_end, i.end-1)) {
    // case 1
    res = -ENOMEM;
    goto ret;
  } else if (is_single && intvl_eq(i, i_start)) {
    // case 2
    vm_update_internal(vm, prot);
  } else if (is_single && i.start == i_start.start) {
    // case 3
    //   |---vm---|---new_vm---|
    //   ^~update~^
    vm_mapping_t *new_vm = split_mapping(vm, len);
    vm_update_internal(vm, prot);
  } else if (is_single && i.end == i_end.end) {
    // case 3
    //   |---vm---|---new_vm---|
    //            ^~~~update~~~^
    vm_mapping_t *new_vm = split_mapping(vm, i.start - i_start.start);
    vm_update_internal(new_vm, prot);
  } else if (is_single) {
    // case 4
    //   |--vm--|--vm_a--|--vm_b--|
    //          ^~update~^
    vm_mapping_t *vm_a = split_mapping(vm, i.start - i_start.start);
    vm_mapping_t *vm_b = split_mapping(vm_a, len);
    vm_update_internal(vm_a, prot);
  } else if (are_siblings && i.start == i_start.start && i.end == i_end.end) {
    // case 5
    vm_mapping_t *sibling = LIST_NEXT(vm, vm_list);
    while (sibling) {
      vm_mapping_t *next = LIST_NEXT(sibling, vm_list);
      join_mappings(vm, sibling);
      sibling = next;
    }
    vm_update_internal(vm, prot);
  } else if (are_siblings) {
    // case 6
    DPRINTF("vm_protect: error: cannot handle non-aligned sibling mappings [name={:str}]\n", &vm->name);
    res = -ENOMEM;
    goto ret;
  } else {
    // case 7
    DPRINTF("vm_protect: error: cannot update protection of region containing multiple mappings\n");
    res = -ENOMEM;
    goto ret;
  }

LABEL(ret);
  space_unlock(space);
  return res;
}

int vm_resize(uintptr_t vaddr, size_t old_size, size_t new_size, bool allow_move, uintptr_t *new_vaddr) {
  // The range [vaddr, vaddr+old_size-1] must represent exactly one mapping of type
  // VM_TYPE_PAGE or VM_TYPE_ANON with a mapping size of old_size. If new_size is
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

  if ((vm->type != VM_TYPE_PAGE && vm->type != VM_TYPE_ANON) || vm->size != old_size) {
    res = -EINVAL;
    goto ret;
  } else if (vm->flags & VM_LINKED || vm->flags & VM_SPLIT) {
    DPRINTF("vm_resize_old: cannot resize part of a split mapping [name={:str}]\n", &vm->name);
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
    size_t len = old_size - new_size;
    size_t off = new_size;
    if (vm->type == VM_TYPE_PAGE) {
      page_type_unmap_internal(vm, len, off);
    } else if (vm->type == VM_TYPE_ANON) {
      anon_type_unmap_internal(vm, len, off);
    }
  }

LABEL(ret);
  space_unlock(space);
  return res;
}

__move page_t *vm_getpage_cow(uintptr_t vaddr) {
  if (!is_valid_pointer(vaddr)) {
    return NULL;
  }

  page_t *page = NULL;
  address_space_t *space = select_space(curspace, vaddr);
  space_lock(space);

  vm_mapping_t *vm = space_get_mapping(space, vaddr);
  if (vm == NULL || vm->type == VM_TYPE_RSVD) {
    goto ret;
  }

  size_t off = vaddr - vm->address;
  switch (vm->type) {
    case VM_TYPE_RSVD:
    case VM_TYPE_PHYS:
      return NULL;
    case VM_TYPE_PAGE:
      page = page_type_getpage_internal(vm, off);
      break;
    case VM_TYPE_ANON:
      page = anon_type_getpage_internal(vm, off);
      break;
    default:
      unreachable;
  }

LABEL(ret);
  space_unlock(space);
  return getref(page);
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
  } else if (vm->type == VM_TYPE_ANON) {
    vm_anon_t *anon = vm->vm_anon;
    size_t index = off / stride;
    paddr = anon_struct_get_phys(anon, index);
  }

LABEL(ret);
  space_unlock(space);
  return paddr;
}

//
// MARK: user space API
//

// create page mappings in the non-current user address space
int other_space_map(address_space_t *uspace, uintptr_t vaddr, uint32_t prot, __move page_t *pages) {
  ASSERT(uspace != kernel_space);
  ASSERT(pages->flags & PG_HEAD);
  ASSERT(vaddr < USER_SPACE_END);
  size_t size = pages->head.count * pg_flags_to_size(pages->flags);
  ASSERT(vaddr+size <= USER_SPACE_END);

  int res = 0;
  space_lock(uspace);

  const char *name = prot & VM_STACK ? "user stack" : "user";
  uint32_t vm_flags = (prot & VM_PROT_MASK) | VM_USER | VM_FIXED | VM_NOMAP;
  if (pages->flags & PG_COW)
    vm_flags |= VM_COW;

  if (vmap_internal(uspace, VM_TYPE_PAGE, vaddr, size, size, vm_flags, name, pages) == 0) {
    DPRINTF("other_space_map: failed to make user mapping in address space [vaddr=%p, size=%zu, prot=%d]\n",
            vaddr, size, prot);
    res = -ENOMEM;
    goto ret;
  }

  // map the pages (non-intrusively)
  page_t *table_pages = NULL;
  nonrecursive_map_pages(uspace->page_table, vaddr, vm_flags, pages, &table_pages);
  if (table_pages != NULL) {
    page_t *last_page = SLIST_GET_LAST(table_pages, next);
    SLIST_ADD_SLIST(&uspace->table_pages, table_pages, last_page, next);
  }

LABEL(ret);
  space_unlock(uspace);
  return res;
}

int other_space_map_cow(address_space_t *uspace, uintptr_t vaddr, uint32_t prot, __ref page_t *pages) {
  ASSERT(uspace != kernel_space);
  page_t *cow_pages = alloc_cow_pages(pages);
  if (cow_pages == NULL) {
    DPRINTF("other_space_map_cow: failed to allocate COW pages\n");
    return -ENOMEM;
  }
  return other_space_map(uspace, vaddr, prot, cow_pages);
}

//
// MARK: Vmalloc API
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
  if (size <= SIZE_TO_PAGES(4)) {
    page_t *pages = alloc_pages(SIZE_TO_PAGES(size));
    if (pages == NULL) {
      ALLOC_ERROR("vmalloc: failed to allocate page\n");
      return 0;
    }
    vaddr = vmap_pages(moveref(pages), 0, PAGE_SIZE, vm_flags, "vmalloc");
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
  } else if (!((vm->type == VM_TYPE_PAGE || vm->type == VM_TYPE_ANON) && (vm->flags & VM_MALLOC))) {
    DPANICF("vfree: invalid pointer: {:018p} is not a vmalloc pointer\n", ptr);
  } else if (((uintptr_t)ptr) != vm->address) {
    DPANICF("vfree: invalid pointer: {:018p} is not the start of a vmalloc mapping\n", ptr);
  }

  free_mapping(&vm);
  space_unlock(space);
}

//
// MARK: vm descriptors
//

static uintptr_t internal_map_desc_virtual(address_space_t *space, vm_desc_t *desc, uint32_t extra_flags) {
  uintptr_t vaddr;
  if (desc->pages != NULL) {
    vaddr = vmap_internal(
      space,
      VM_TYPE_PAGE,
      desc->address,
      desc->size,
      desc->vm_size,
      desc->vm_flags | extra_flags,
      desc->name,
      getref(desc->pages)
    );
  } else {
    vm_anon_t *anon = anon_struct_alloc(NULL, desc->size, vm_flags_to_size(desc->vm_flags));
    vaddr = vmap_internal(
      space,
      VM_TYPE_ANON,
      desc->address,
      desc->size,
      desc->vm_size,
      desc->vm_flags | extra_flags,
      desc->name,
      anon
    );
    if (vaddr == 0)
      anon_struct_free(anon);
  }

  if (vaddr == 0) {
    DPRINTF("internal_map_desc_virtual: failed to make user mapping in address space [vaddr=%p, size=%zu, prot=%d]\n",
            desc->address, desc->size, desc->vm_flags);
  }
  return vaddr;
}

//

vm_desc_t *vm_desc_alloc(uint64_t address, size_t size, uint32_t vm_flags, const char *name, __move page_t *pages) {
  vm_desc_t *desc = kmallocz(sizeof(vm_desc_t));
  desc->address = address;
  desc->size = size;
  desc->vm_size = size;
  desc->vm_flags = vm_flags;
  desc->pages = pages;
  desc->name = name;
  desc->next = NULL;
  ASSERT(pages ? pages->flags & PG_HEAD : true);

  if (name == NULL) {
    desc->name = (vm_flags & VM_STACK) ? "stack" : (pages != NULL) ? "pages" : "anon";
  }
  return desc;
}

void vm_desc_free_all(vm_desc_t **descp) {
  vm_desc_t *desc = *descp;
  while (desc) {
    vm_desc_t *next = desc->next;
    drop_pages(&desc->pages);
    kfree(desc);
    desc = next;
  }
  *descp = NULL;
}

int vm_desc_map(vm_desc_t *descs) {
  int res = 0;
  space_lock(curspace);

  vm_desc_t *desc = descs;
  while (desc != NULL) {
    if (internal_map_desc_virtual(curspace, desc, 0) == 0) {
      res = -1;
      goto ret;
    }

    desc = desc->next;
  }

LABEL(ret);
  space_unlock(curspace);
  return res;
}

int vm_desc_map_other_space(vm_desc_t *descs, address_space_t *uspace) {
  int res = 0;
  space_lock(uspace);

  vm_desc_t *desc = descs;
  while (desc != NULL) {
    page_t *pages = getref(desc->pages);
    uintptr_t vaddr = internal_map_desc_virtual(uspace, desc, VM_NOMAP);
    if (vaddr == 0) {
      drop_pages(&pages);
      res = -1;
      goto ret;
    }

    page_t *table_pages = NULL;
    nonrecursive_map_pages(uspace->page_table, vaddr, desc->vm_flags, desc->pages, &table_pages);
    if (table_pages != NULL) {
      page_t *last_page = SLIST_GET_LAST(table_pages, next);
      SLIST_ADD_SLIST(&uspace->table_pages, table_pages, last_page, next);
    }

    desc = desc->next;
  }

LABEL(ret);
  space_unlock(uspace);
  return res;
}

//
// debug functions

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
        kprintf("{:018p}-{:018p} {:$ >10M}  ---  empty\n", empty_start, empty_start+empty_size, empty_size);
      }

      kprintf("{:018p}-{:018p} {:$ >10M}  ---  guard\n", guard_start, guard_start+PAGE_SIZE, PAGE_SIZE);
      kprintf("{:018p}-{:018p} {:$ >10M}  {:.3s}  {:str}\n",
              vm->address, vm->address+vm->size, vm->size, prot_str, &vm->name);
    } else {
      kprintf("{:018p}-{:018p} {:$ >10M}  {:.3s}  {:str}\n",
                vm->address, vm->address+vm->size, vm->size, prot_str, &vm->name);

      if (empty_size > 0) {
        uintptr_t empty_start = vm->address+vm->size;
        kprintf("{:018p}-{:018p} {:$ >10M}  ---  empty\n", empty_start, empty_start+empty_size, empty_size);
      }
    }

    prev_end = intvl.end;
    vm = LIST_NEXT(vm, vm_list);
  }
  space_unlock(space);
}

void vm_write_format_address_space(int fd, address_space_t *space) {
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
      kfdprintf(fd, "{:^37s} {:$ >10M}\n", "unmapped", gap_size);
    }

    if (vm->flags & VM_STACK) {
      uintptr_t empty_start = intvl.start;
      uintptr_t guard_start = intvl.start+empty_size;

      // in stack mappings the empty space and guard page come first
      if (empty_size > 0) {
        kfdprintf(fd, "{:018p}-{:018p} {:$ >10M}  ---  empty\n", empty_start, empty_start+empty_size, empty_size);
      }

      kfdprintf(fd, "{:018p}-{:018p} {:$ >10M}  ---  guard\n", guard_start, guard_start+PAGE_SIZE, PAGE_SIZE);
      kfdprintf(fd, "{:018p}-{:018p} {:$ >10M}  {:.3s}  {:str}\n",
              vm->address, vm->address+vm->size, vm->size, prot_str, &vm->name);
    } else {
      kfdprintf(fd, "{:018p}-{:018p} {:$ >10M}  {:.3s}  {:str}\n",
              vm->address, vm->address+vm->size, vm->size, prot_str, &vm->name);

      if (empty_size > 0) {
        uintptr_t empty_start = vm->address+vm->size;
        kfdprintf(fd, "{:018p}-{:018p} {:$ >10M}  ---  empty\n", empty_start, empty_start+empty_size, empty_size);
      }
    }

    prev_end = intvl.end;
    vm = LIST_NEXT(vm, vm_list);
  }
  kprintf("{:$=^64}\n");
  space_unlock(space);
}

void vm_write_format_address_space_graphiz(int fd, address_space_t *space) {
  space_lock(space);
  intvl_iter_t *iter = intvl_iter_tree(space->new_tree);
  intvl_node_t *node;
  rb_node_t *nil = space->new_tree->tree->nil;
  int null_count = 0;

  kfdprintf(fd, "digraph BST {{\n");
  kfdprintf(fd, "  node [fontname=\"Arial\"];\n");
  while ((node = intvl_iter_next(iter))) {
    interval_t i = node->interval;
    rb_node_t *rbnode = node->node;

    vm_mapping_t *vm = node->data;
    kfdprintf(fd, "  %llu [label=\"{:str}\\n%p-%p\"];\n",
            rbnode->key, &vm->name, i.start, i.end);

    if (rbnode->left != nil) {
      kfdprintf(fd, "  %llu -> %llu\n", rbnode->key, rbnode->left->key);
    } else {
      kfdprintf(fd, "  null%d [shape=point];\n", null_count);
      kfdprintf(fd, "  %llu -> null%d;\n", rbnode->key, null_count);
      null_count++;
    }

    if (rbnode->right != nil) {
      kfdprintf(fd, "  %llu -> %llu\n", rbnode->key, rbnode->right->key);
    } else {
      kfdprintf(fd, "  null%d [shape=point];\n", null_count);
      kfdprintf(fd, "  %llu -> null%d;\n", rbnode->key, null_count);
      null_count++;
    }
  }
  kfdprintf(fd, "}}\n");
  kfree(iter);
  space_unlock(space);
}

//
// MARK: Syscalls
//

#include <kernel/fs_utils.h>

DEFINE_SYSCALL(mmap, void *, void *addr, size_t len, int prot, int flags, int fd, off_t off) {
  DPRINTF("mmap: addr=%p, len=%zu, prot=%#b, flags=%#x, fd=%d, off=%zu\n", addr, len, prot, flags, fd, off);
  if (flags & MAP_FIXED) {
    unimplemented("mmap fixed");
  }

  if (flags & MAP_ANONYMOUS) {
    fd = -1;
    off = 0;

    uint32_t vm_flags = VM_USER;
    vm_flags |= prot & PROT_READ ? VM_READ : 0;
    vm_flags |= prot & PROT_WRITE ? VM_WRITE : 0;
    vm_flags |= prot & PROT_EXEC ? VM_EXEC : 0;

    uintptr_t res = vmap_anon(0, (uintptr_t)addr, len, vm_flags, "mmap");
    if (res == 0)
      return MAP_FAILED;
    return (void *) res;
  }
  return 0;
}

DEFINE_SYSCALL(mprotect, int, void *addr, size_t len, int prot) {
  DPRINTF("mprotect: addr=%p, len=%zu, prot=%d\n", addr, len, prot);
  return vm_protect((uintptr_t) addr, len, prot);
}

DEFINE_SYSCALL(munmap, int, void *addr, size_t len) {
  DPRINTF("munmap: addr=%p, len=%zu\n", addr, len);
  return vm_free((uintptr_t) addr, len);
}
