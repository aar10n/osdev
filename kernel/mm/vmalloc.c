//
// Created by Aaron Gill-Braun on 2022-06-17.
//

#include <mm/vmalloc.h>
#include <mm/pmalloc.h>
#include <mm/pgtable.h>
#include <mm/heap.h>
#include <mm/init.h>

#include <cpu/cpu.h>
#include <panic.h>
#include <string.h>
#include <printf.h>

#include <interval_tree.h>

// internal vmap flags
#define VMAP_FIXED      (1 << 0)
#define VMAP_USERSPACE  (1 << 1)


address_space_t *kernel_space;

void execute_init_address_space_callbacks();

void vm_swap_vmspace() {
  panic("CHANGE ME");
}

__used int fault_handler(uintptr_t rip_addr, uintptr_t fault_addr, uint32_t err) {
  kprintf("[vm] page fault at %p accessing %p (err 0b%b)\n", rip_addr, fault_addr, err);
  return -1;
}

//

static always_inline address_space_t *select_address_space(uintptr_t address) {
  if (address >= USER_SPACE_START && address <= USER_SPACE_END) {
    /* user space */
    return NULL;
  } else if (address >= KERNEL_SPACE_START && address <= KERNEL_SPACE_END) {
    return kernel_space;
  }
  panic("non-canonical address: %p", address);
}

static always_inline uintptr_t select_vmap_hint(uint32_t vm_flags) {
  if (vm_flags & VMAP_USERSPACE) {
    return USER_SPACE_START;
  } else {
    return KERNEL_SPACE_START;
  }
}

uintptr_t locate_free_address_region(address_space_t *space, uintptr_t base, size_t size) {
  kassert(base >= space->min_addr && (base + size) <= space->max_addr);

  int alignment = PAGE_SIZE;
  if (size >= SIZE_2MB) {
    alignment = SIZE_2MB;
  } else if (size >= SIZE_1GB) {
    alignment = SIZE_1GB;
  }

  uintptr_t addr = base;
  interval_t interval = intvl(base, base + size);
  intvl_node_t *closest = intvl_tree_find_closest(space->root, interval);
  if (closest == NULL) {
    return addr;
  } else if (!overlaps(interval, closest->interval)) {
    return addr;
  }

  rb_iter_type_t iter_type = FORWARD;
  rb_node_t *node = NULL;
  rb_node_t *last = NULL;
  rb_iter_t *iter = rb_tree_make_iter(space->root->tree, closest->node, iter_type);
  while ((node = rb_iter_next(iter))) {
    interval_t i = ((intvl_node_t *) node->data)->interval;
    interval_t j = last ? ((intvl_node_t *) last->data)->interval : i;

    // if two consequtive nodes are not contiguous in memory
    // check that there is enough space between the them to
    // fit the requested area.
    bool contig = contiguous(i, j);
    if (!contig && i.start > addr && i.start - addr >= size) {
      kassert(addr + size <= space->max_addr);
      return addr;
    }
    addr = align(i.end, alignment);
    last = node;
  }

  return 0;
}

bool check_address_region_free(address_space_t *space, uintptr_t base, size_t size) {
  kassert(base >= space->min_addr && (base + size) <= space->max_addr);
  interval_t interval = intvl(base, base + size);
  intvl_node_t *closest = intvl_tree_find_closest(space->root, interval);
  if (closest == NULL) {
    return true;
  }

  return !overlaps(interval, closest->interval);
}

vm_mapping_t *allocate_vm_mapping(address_space_t *space, uintptr_t addr, size_t size, uint32_t vm_flags) {
  vm_mapping_t *mapping = kmalloc(sizeof(vm_mapping_t));

  spin_lock(&space->lock);
  uintptr_t virt_addr;
  if (vm_flags & VMAP_FIXED) {
    virt_addr = addr;
    if (!check_address_region_free(space, addr, size)) {
      spin_unlock(&space->lock);
      kfree(mapping);
      kprintf("allocate_vm_mapping: address region already allocated");
      return NULL;
    }
  } else {
    virt_addr = locate_free_address_region(space, addr, size);
    if (virt_addr == 0) {
      spin_unlock(&space->lock);
      kfree(mapping);
      panic("no free address space");
    }
  }

  intvl_tree_insert(space->root, intvl(virt_addr, virt_addr + size), mapping);
  spin_unlock(&space->lock);

  mapping->address = virt_addr;
  mapping->size = size;
  mapping->data.ptr = NULL;
  mapping->type = 0;
  mapping->attr = 0;
  mapping->reserved = 0;
  mapping->flags = 0;
  mapping->name = NULL;
  spin_init(&mapping->lock);
  return mapping;
}

//

vm_mapping_t *vmap_pages_internal(page_t *pages, uintptr_t hint, uint32_t vm_flags) {
  kassert(pages != NULL);
  kassert(pages->flags & PG_LIST_HEAD);
  if (vm_flags & VMAP_USERSPACE) {
    kassert(hint >= USER_SPACE_START && hint <= USER_SPACE_END);
  } else {
    kassert(hint >= KERNEL_SPACE_START && hint <= KERNEL_SPACE_END);
  }

  size_t total_size = pages->head.list_sz;

  address_space_t *space = select_address_space(hint);
  vm_mapping_t *mapping = allocate_vm_mapping(space, hint, total_size, vm_flags);
  if (mapping == NULL) {
    return NULL;
  }

  mapping->type = VM_TYPE_PAGE;
  mapping->attr = vm_flags & VMAP_USERSPACE ? VM_ATTR_USER : 0;
  mapping->data.page = pages;
  mapping->name = "page";

  page_t *curr = pages;
  uintptr_t ptr = mapping->address;
  while (curr != NULL) {
    // recursive_map_entry(ptr, curr->address, curr->flags);
    curr->flags |= PG_MAPPED;
    curr->mapping = mapping;

    ptr += pg_flags_to_size(curr->flags);
    curr = curr->next;
  }

  cpu_flush_tlb();
  return mapping;
}

vm_mapping_t *vmap_phys_internal(uintptr_t phys_addr, size_t size, uint32_t flags, uintptr_t hint, uint32_t vm_flags) {
  kassert(phys_addr % PAGE_SIZE == 0);
  kassert(size % PAGE_SIZE == 0 && size > 0);
  if (vm_flags & VMAP_USERSPACE) {
    kassert(hint >= USER_SPACE_START && hint <= USER_SPACE_END);
  } else {
    kassert(hint >= KERNEL_SPACE_START && hint <= KERNEL_SPACE_END);
  }

  size_t stride = pg_flags_to_size(flags);
  size_t count = size / stride;

  address_space_t *space = select_address_space(hint);
  vm_mapping_t *mapping = allocate_vm_mapping(space, hint, size, vm_flags);
  if (mapping == NULL) {
    return NULL;
  }

  mapping->type = VM_TYPE_PHYS;
  mapping->attr = vm_flags & VMAP_USERSPACE ? VM_ATTR_USER : 0;
  mapping->flags = flags;
  mapping->data.phys = phys_addr;
  mapping->name = "phys";

  uintptr_t ptr = mapping->address;
  uintptr_t phys_ptr = phys_addr;
  while (count > 0) {
    // recursive_map_entry(ptr, phys_ptr, flags);

    ptr += stride;
    phys_ptr += stride;
    count--;
  }

  cpu_flush_tlb();
  return mapping;
}

//

void init_address_space() {
  kernel_space = kmalloc(sizeof(address_space_t));
  kernel_space->root = create_intvl_tree();
  kernel_space->min_addr = KERNEL_SPACE_START;
  kernel_space->max_addr = KERNEL_SPACE_END;
  spin_init(&kernel_space->lock);

  // kernel mapped low memory
  vm_mapping_t *kernel_lowmem_vm = _vmap_reserve(kernel_virtual_offset, kernel_address - kernel_virtual_offset);
  kernel_lowmem_vm->name = "reserved";
  kernel_lowmem_vm->type = VM_TYPE_PHYS;
  kernel_lowmem_vm->data.phys = kernel_virtual_offset;
  // kernel code
  vm_mapping_t *kernel_code_vm = _vmap_reserve(kernel_code_start, kernel_code_end - kernel_code_start);
  kernel_code_vm->name = "kernel code";
  kernel_code_vm->type = VM_TYPE_PHYS;
  kernel_code_vm->data.phys = kernel_code_start;
  // kernel data
  vm_mapping_t *kernel_data_vm = _vmap_reserve(kernel_data_end, kernel_data_end - kernel_code_end);
  kernel_data_vm->name = "kernel data";
  kernel_data_vm->type = VM_TYPE_PHYS;
  kernel_data_vm->data.phys = kernel_data_end;
  // kernel heap
  vm_mapping_t *kheap_vm = _vmap_reserve(KERNEL_HEAP_VA, KERNEL_HEAP_SIZE);
  kheap_vm->name = "kernel heap";
  kheap_vm->type = VM_TYPE_PHYS;
  kheap_vm->data.phys = kheap_ptr_to_phys((void *) KERNEL_HEAP_VA);

  execute_init_address_space_callbacks();
}

void *_vmap_pages(page_t *pages) {
  uint32_t vm_flags = VMAP_FIXED | (pages->flags & PG_USER ? VMAP_USERSPACE : 0);
  uintptr_t hint = select_vmap_hint(vm_flags);
  vm_mapping_t *mapping = vmap_pages_internal(pages, hint, vm_flags);
  if (mapping == NULL) {
    return NULL;
  }

  return (void *) mapping->address;
}

void *_vmap_pages_addr(uintptr_t virt_addr, page_t *pages) {
  uint32_t vm_flags = VMAP_FIXED | (pages->flags & PG_USER ? VMAP_USERSPACE : 0);
  vm_mapping_t *mapping = vmap_pages_internal(pages, virt_addr, vm_flags);
  if (mapping == NULL) {
    return NULL;
  }

  return (void *) mapping->address;
}

void *_vmap_phys(uintptr_t phys_addr, size_t size, uint32_t flags) {
  uint32_t vm_flags = flags & PG_USER ? VMAP_USERSPACE : 0;
  uintptr_t hint = select_vmap_hint(vm_flags);
  vm_mapping_t *mapping = vmap_phys_internal(phys_addr, size, flags, hint, vm_flags);
  return (void *) mapping->address;
}

void *_vmap_phys_addr(uintptr_t virt_addr, uintptr_t phys_addr, size_t size, uint32_t flags) {
  uint32_t vm_flags = VMAP_FIXED | (flags & PG_USER ? VMAP_USERSPACE : 0);
  vm_mapping_t *mapping = vmap_phys_internal(phys_addr, size, flags, virt_addr, vm_flags);
  if (mapping == NULL) {
    return NULL;
  }

  return (void *) mapping->address;
}

void *_vmap_mmio(uintptr_t phys_addr, size_t size, uint32_t flags) {
  vm_mapping_t *mapping = vmap_phys_internal(phys_addr, size, flags, MMIO_BASE_VA, 0);
  if (mapping == NULL) {
    return NULL;
  }

  mapping->attr |= VM_ATTR_MMIO;
  mapping->name = "mmio";
  return (void *) mapping->address;
}

void *_vmap_mmap(uintptr_t phys_addr, size_t size, uint32_t flags) {
  flags |= PG_USER;
  vm_mapping_t *mapping = vmap_phys_internal(phys_addr, size, flags, USER_SPACE_START, VMAP_USERSPACE);
  if (mapping == NULL) {
    return NULL;
  }

  mapping->attr |= VM_TYPE_ANON;
  mapping->name = "mmap";
  return (void *) mapping->address;
}

void *_vmap_mmap_fixed(uintptr_t virt_addr, uintptr_t phys_addr, size_t size, uint32_t flags) {
  flags |= PG_USER;
  kassert(virt_addr >= USER_SPACE_START && virt_addr < USER_SPACE_END);
  vm_mapping_t *mapping = vmap_phys_internal(phys_addr, size, flags, virt_addr, VMAP_FIXED | VMAP_USERSPACE);
  if (mapping == NULL) {
    return NULL;
  }

  mapping->attr |= VM_TYPE_ANON;
  mapping->name = "mmap";
  return (void *) mapping->address;
}

vm_mapping_t *_vmap_reserve(uintptr_t virt_addr, size_t size) {
  uint32_t vm_flags = VMAP_FIXED;
  uint16_t attr = 0;
  if (virt_addr < KERNEL_SPACE_START) {
    vm_flags |= VMAP_USERSPACE;
    attr = VM_ATTR_USER;
  }

  address_space_t *space = select_address_space(virt_addr);
  vm_mapping_t *mapping = allocate_vm_mapping(space, virt_addr, size, vm_flags);
  if (mapping == NULL) {
    return NULL;
  }

  mapping->type = VM_TYPE_RSVD;
  mapping->attr = attr;
  mapping->data.ptr = NULL;
  mapping->name = "reserved";
  return mapping;
}

//

void _vunmap_pages(page_t *pages) {
  kassert(pages != NULL);
  kassert(IS_PG_MAPPED(pages->flags));
  kassert(pages->flags & PG_LIST_HEAD);

  vm_mapping_t *mapping = pages->mapping;
  address_space_t *space = select_address_space(mapping->address);
  interval_t intvl = intvl(mapping->address, mapping->address + mapping->size);
  intvl_tree_delete(space->root, intvl);
  mapping->data.ptr = NULL;
  kfree(mapping);

  page_t *curr = pages;
  while (curr) {
    kassert(curr->flags & PG_MAPPED);
    recursive_unmap_entry(curr->address, curr->flags);
    curr->flags ^= PG_MAPPED;
    curr->mapping = NULL;
    curr = curr->next;
  }
}

void _vunmap_addr(uintptr_t virt_addr, size_t size) {
  address_space_t *space = select_address_space(virt_addr);
  interval_t intvl = intvl(virt_addr, virt_addr + size);
  intvl_node_t *node = intvl_tree_find(space->root, intvl);
  if (node == NULL) {
    panic("unmap: page is not mapped");
  }

  vm_mapping_t *mapping = node->data;
  if (mapping->type == VM_TYPE_PAGE) {
    _vunmap_pages(mapping->data.page);
    return;
  }

  kassert(mapping->size == size);
  size_t flags = mapping->flags;
  size_t ptr = mapping->address;
  size_t stride = pg_flags_to_size(mapping->flags);

  intvl_tree_delete(space->root, intvl);
  mapping->data.ptr = NULL;
  kfree(mapping);

  while (size > 0) {
    recursive_unmap_entry(ptr, flags);
    ptr += stride;
    size -= stride;
  }
}

//

int _vmap_name_mapping(uintptr_t virt_addr, size_t size, const char *name) {
  address_space_t *space = select_address_space(virt_addr);
  interval_t intvl = intvl(virt_addr, virt_addr + size);
  intvl_node_t *node = intvl_tree_find(space->root, intvl);
  if (node == NULL) {
    return -EFAULT;
  }

  vm_mapping_t *mapping = node->data;
  if (mapping->address != virt_addr || mapping->size != size) {
    return -EINVAL;
  }

  mapping->name = name;
  return 0;
}

uintptr_t _vm_virt_to_phys(uintptr_t virt_addr) {
  address_space_t *space = select_address_space(virt_addr);
  interval_t intvl = intvl(virt_addr, virt_addr + PAGE_SIZE);
  intvl_node_t *node = intvl_tree_find(space->root, intvl);
  if (node == NULL) {
    return 0;
  }

  vm_mapping_t *mapping = node->data;
  size_t offset = mapping->address - virt_addr;
  if (mapping->type == VM_TYPE_PHYS) {
    return mapping->data.phys + offset;
  } else if (mapping->type == VM_TYPE_PAGE) {
    page_t *curr = mapping->data.page;
    while (curr) {
      if (curr->address >= virt_addr && curr->address + pg_flags_to_size(curr->flags) <= virt_addr) {
        return curr->address + offset;
      }
      curr = curr->next;
    }
    unreachable;
  }

  kprintf("vm_virt_to_phys: invalid mapping type");
  return 0;
}

vm_mapping_t *_vm_virt_to_mapping(uintptr_t virt_addr) {
  address_space_t *space = select_address_space(virt_addr);
  interval_t intvl = intvl(virt_addr, virt_addr + PAGE_SIZE);
  intvl_node_t *node = intvl_tree_find(space->root, intvl);
  if (node == NULL) {
    return NULL;
  }

  return node->data;
}

page_t *_vm_virt_to_page(uintptr_t virt_addr) {
  address_space_t *space = select_address_space(virt_addr);
  interval_t intvl = intvl(virt_addr, virt_addr + PAGE_SIZE);
  intvl_node_t *node = intvl_tree_find(space->root, intvl);
  if (node == NULL) {
    return NULL;
  }

  vm_mapping_t *mapping = node->data;
  if (mapping->type == VM_TYPE_PAGE) {
    return mapping->data.page;
  }

  kprintf("vm_virt_to_page: not a page mapping");
  return NULL;
}

// utility functions

page_t *valloc_page(uint32_t flags) {
  page_t *page = _alloc_pages(1, flags);
  if (_vmap_pages(page) == NULL) {
    panic("could not map pages");
  }
  return page;
}

page_t *valloc_pages(size_t count, uint32_t flags) {
  page_t *pages = _alloc_pages(count, flags);
  if (_vmap_pages(pages) == NULL) {
    panic("could not map pages");
  }
  return pages;
}

page_t *valloc_zero_pages(size_t count, uint32_t flags) {
  page_t *pages = _alloc_pages(count, flags);
  void *ptr = _vmap_pages(pages);
  if (ptr == NULL) {
    panic("could not map pages");
  }

  size_t size = count * pg_flags_to_size(flags);
  if (IS_PG_WRITABLE(flags)) {
    memset(ptr, 0, size);
  } else {
    cpu_disable_interrupts();
    cpu_disable_write_protection();
    memset(ptr, 0, size);
    cpu_enable_write_protection();
    cpu_enable_interrupts();
  }

  return pages;
}

void vfree_pages(page_t *pages) {
  if (IS_PG_MAPPED(pages->flags)) {
    _vunmap_pages(pages);
  }
  _free_pages(pages);
}

// debug functions

void _address_space_print_mappings(address_space_t *space) {
  intvl_iter_t *iter = intvl_iter_tree(space->root);
  intvl_node_t *node;

  kprintf("============ Virtual Mappings ============\n");
  while ((node = intvl_iter_next(iter))) {
    interval_t i = node->interval;
    kprintf("%018p - %018p | %llu\n", i.start, i.end, i.end - i.start);
  }
  kprintf("==========================================\n");
  kfree(iter);
}

void _address_space_to_graphiz(address_space_t *space) {
  intvl_iter_t *iter = intvl_iter_tree(space->root);
  intvl_node_t *node;
  rb_node_t *nil = space->root->tree->nil;
  int null_count = 0;

  kprintf("digraph BST {\n");
  kprintf("  node [fontname=\"Arial\"];\n");
  while ((node = intvl_iter_next(iter))) {
    interval_t i = node->interval;
    rb_node_t *rbnode = node->node;

    kprintf("  %llu [label=\"start: %p\\nend: %p\\nmin: %p\\nmax: %p;\"];\n",
            rbnode->key, i.start, i.end, node->min, node->max);

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