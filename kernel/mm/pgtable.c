//
// Created by Aaron Gill-Braun on 2022-06-18.
//

#include <kernel/mm/pgtable.h>
#include <kernel/mm/vmalloc.h>
#include <kernel/mm/pmalloc.h>
#include <kernel/mm/init.h>
#include <kernel/mm_types.h>

#include <kernel/proc.h>

#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/panic.h>

#include <kernel/cpu/cpu.h>

#define ASSERT(x) kassert(x)

#define NUM_ENTRIES 512
#define T_ENTRY 509ULL // temp pdpt entry index
#define R_ENTRY 510ULL // recursive entry index

#define PT_INDEX(a) (((a) >> 12) & 0x1FF)
#define PDT_INDEX(a) (((a) >> 21) & 0x1FF)
#define PDPT_INDEX(a) (((a) >> 30) & 0x1FF)
#define PML4_INDEX(a) (((a) >> 39) & 0x1FF)

#define index_for_pg_level(addr, level) (((addr) >> (12 + ((level) * 9))) & 0x1FF)
#define pg_level_to_shift(level) (12 + ((level) * 9))

#define get_virt_addr(l4, l3, l2, l1) \
  ((0xFFFFULL << 48) | ((uint64_t)(l4) << 39) | ((uint64_t)(l3) << 30) | \
  ((uint64_t)(l2) << 21) | ((uint64_t)(l1) << 12))

#define PML4_PTR    ((uint64_t *) get_virt_addr(R_ENTRY, R_ENTRY, R_ENTRY, R_ENTRY))
#define TEMP_PDPT   ((uint64_t *) get_virt_addr(R_ENTRY, R_ENTRY, R_ENTRY, T_ENTRY))
#define TEMP_PDPTE  (&TEMP_PDPT[curcpu_id])
#define TEMP_PTR    ((uint64_t *) get_virt_addr(R_ENTRY, R_ENTRY, T_ENTRY, curcpu_id))

// page entry flags
#define PE_PRESENT        (1ULL << 0)
#define PE_WRITE          (1ULL << 1)
#define PE_USER           (1ULL << 2)
#define PE_WRITE_THROUGH  (1ULL << 3)
#define PE_CACHE_DISABLE  (1ULL << 4)
#define PE_ACCESSED       (1ULL << 5)
#define PE_DIRTY          (1ULL << 6)
#define PE_SIZE           (1ULL << 7)
#define PE_GLOBAL         (1ULL << 8)
#define PE_NO_EXECUTE     (1ULL << 63)

#define PE_FLAGS_MASK 0xFFF
#define PE_FRAME_MASK 0xFFFFFFFFFFFFF000

typedef enum pg_level {
  PG_LEVEL_PT,
  PG_LEVEL_PD,
  PG_LEVEL_PDP,
  PG_LEVEL_PML4,
} pg_level_t;

void _print_pgtable_indexes(uintptr_t addr);
void _print_pgtable_address(uint16_t l4, uint16_t l3, uint16_t l2, uint16_t l1);

// kernel page table
uint64_t *startup_kernel_pml4;
// pdpe page for the temp entry
page_t *temp_pdpt_page;


static inline uint64_t *get_child_pgtable_address(const uint64_t *parent, pg_level_t level, uint16_t index) {
  uintptr_t addr = (uintptr_t) parent;
  addr |= (index << pg_level_to_shift(level));
  return (uint64_t *) addr;
}

static inline uint64_t *get_pgtable_address(uintptr_t virt_addr, pg_level_t level) {
  // R_ENTRY, R_ENTRY, R_ENTRY, R_ENTRY          -> pml4
  // R_ENTRY, R_ENTRY, R_ENTRY, PML4_INDEX       -> pdpt
  // R_ENTRY, R_ENTRY, PML4_INDEX, PDPT_INDEX    -> pdt
  // R_ENTRY, PML4_INDEX, PDPT_INDEX, PDT_INDEX  -> pt
  // PML4_INDEX, PDPT_INDEX, PDT_INDEX, PT_INDEX -> pte
  switch (level) {
    case PG_LEVEL_PML4:
      return (uint64_t *) get_virt_addr(R_ENTRY, R_ENTRY, R_ENTRY, R_ENTRY);
    case PG_LEVEL_PDP:
      return (uint64_t *) get_virt_addr(R_ENTRY, R_ENTRY, R_ENTRY, PML4_INDEX(virt_addr));
    case PG_LEVEL_PD:
      return (uint64_t *) get_virt_addr(R_ENTRY, R_ENTRY, PML4_INDEX(virt_addr), PDPT_INDEX(virt_addr));
    case PG_LEVEL_PT:
      return (uint64_t *) get_virt_addr(R_ENTRY, PML4_INDEX(virt_addr), PDPT_INDEX(virt_addr), PDT_INDEX(virt_addr));
    default:
      unreachable;
  }
}

static inline uint16_t vm_flags_to_pe_flags(uint32_t vm_flags) {
  uint16_t entry_flags = PE_PRESENT;
  entry_flags |= (vm_flags & VM_WRITE) ? PE_WRITE : 0;
  entry_flags |= (vm_flags & VM_USER) ? PE_USER : 0;
  entry_flags |= (vm_flags & VM_NOCACHE) ? PE_CACHE_DISABLE : 0;
  entry_flags |= (vm_flags & VM_WRITETHRU) ? PE_WRITE_THROUGH : 0;
  entry_flags |= (vm_flags & VM_EXEC) ? 0 : PE_NO_EXECUTE;
  entry_flags |= (vm_flags & VM_GLOBAL) ? PE_GLOBAL : 0;
  if ((vm_flags & VM_HUGE_2MB) || (vm_flags & VM_HUGE_1GB)) {
    entry_flags |= PE_SIZE;
  }
  return entry_flags;
}

static inline pg_level_t vm_flags_to_level(uint32_t vm_flags) {
  if (vm_flags & VM_HUGE_1GB) {
    return PG_LEVEL_PDP;
  } else if (vm_flags & VM_HUGE_2MB) {
    return PG_LEVEL_PD;
  } else {
    return PG_LEVEL_PT;
  }
}


static uint64_t *early_map_entry(uintptr_t virt_addr, uintptr_t phys_addr, uint32_t vm_flags) {
  ASSERT(virt_addr % PAGE_SIZE == 0);
  ASSERT(phys_addr % PAGE_SIZE == 0);

  pg_level_t map_level = PG_LEVEL_PT;
  if (vm_flags & VM_HUGE_2MB) {
    ASSERT(is_aligned(virt_addr, SIZE_2MB));
    ASSERT(is_aligned(phys_addr, SIZE_2MB));
    map_level = PG_LEVEL_PD;
  } else if (vm_flags & VM_HUGE_1GB) {
    ASSERT(is_aligned(virt_addr, SIZE_1GB));
    ASSERT(is_aligned(phys_addr, SIZE_1GB));
    map_level = PG_LEVEL_PDP;
  }

  uint16_t entry_flags = vm_flags_to_pe_flags(vm_flags);
  uint64_t *pml4 = (void *) ((uint64_t) boot_info_v2->pml4_addr);
  uint64_t *table = pml4;
  for (pg_level_t level = PG_LEVEL_PML4; level > map_level; level--) {
    int index = index_for_pg_level(virt_addr, level);
    uintptr_t next_table = table[index] & PE_FRAME_MASK;
    if (next_table == 0) {
      // create new table
      uintptr_t new_table = mm_early_alloc_pages(1);
      memset((void *) new_table, 0, PAGE_SIZE);
      table[index] = new_table | PE_WRITE | PE_PRESENT;
      next_table = new_table;
    } else if (!(table[index] & PE_PRESENT)) {
      table[index] = next_table | PE_WRITE | PE_PRESENT;
    }

    table = (void *) next_table;
  }

  int index = index_for_pg_level(virt_addr, map_level);
  table[index] = phys_addr | entry_flags;
  return table + index;
}

//

void *early_map_entries(uintptr_t vaddr, uintptr_t paddr, size_t count, uint32_t vm_flags) {
  ASSERT(vaddr % PAGE_SIZE == 0);
  ASSERT(paddr % PAGE_SIZE == 0);
  ASSERT(count > 0);

  pg_level_t map_level = PG_LEVEL_PT;
  size_t stride = PAGE_SIZE;
  if (vm_flags & VM_HUGE_2MB) {
    ASSERT(is_aligned(vaddr, SIZE_2MB));
    ASSERT(is_aligned(paddr, SIZE_2MB));
    map_level = PG_LEVEL_PD;
    stride = SIZE_2MB;
  } else if (vm_flags & VM_HUGE_1GB) {
    ASSERT(is_aligned(vaddr, SIZE_1GB));
    ASSERT(is_aligned(paddr, SIZE_1GB));
    map_level = PG_LEVEL_PDP;
    stride = SIZE_1GB;
  }

  void *addr = (void *) vaddr;
  uint16_t entry_flags = vm_flags_to_pe_flags(vm_flags);
  while (count > 0) {
    int index = index_for_pg_level(vaddr, map_level);
    uint64_t *entry = early_map_entry(vaddr, paddr, vm_flags);
    entry++;
    count--;
    vaddr += stride;
    paddr += stride;

    for (int i = index + 1; i < NUM_ENTRIES; i++) {
      if (count == 0) {
        break;
      }

      *entry = paddr | entry_flags;
      entry++;
      count--;
      vaddr += stride;
      paddr += stride;
      cpu_invlpg(vaddr);
    }
  }

  return addr;
}


void init_recursive_pgtable() {
  uintptr_t pgtable = get_current_pgtable();
  // identity mappings are still in effect
  uint64_t *table_virt = (void *) pgtable;

  // here we setup recursive paging which enables us to access the containing
  // page for any entry at any level of the hierarchy at an accessible known address.
  table_virt[R_ENTRY] = (uint64_t) pgtable | PE_WRITE | PE_PRESENT;

  // we also setup a fixed page directory pointer table to enable the temporary mapping
  // of pages. each cpu has its own entry in this table which can be accessed with TEMP_PDPTE
  temp_pdpt_page = alloc_pages(1);
  table_virt[T_ENTRY] = temp_pdpt_page->address | PE_WRITE | PE_PRESENT;
  memset(TEMP_PDPT, 0, PAGE_SIZE);
}

uintptr_t get_current_pgtable() {
  return __read_cr3() & PE_FRAME_MASK;
}

void set_current_pgtable(uintptr_t table_phys) {
  __write_cr3((uint64_t) table_phys);
}

//
// MARK: Recursive page table API
//

uint64_t *recursive_map_entry(uintptr_t vaddr, uintptr_t paddr, uint32_t vm_flags, __move page_t **out_pages) {
  LIST_HEAD(page_t) table_pages = LIST_HEAD_INITR;
  pg_level_t map_level = PG_LEVEL_PT;
  if (vm_flags & VM_HUGE_2MB) {
    map_level = PG_LEVEL_PD;
    ASSERT(is_aligned(vaddr, SIZE_2MB) && "bigpage must be aligned");
  } else if (vm_flags & VM_HUGE_1GB) {
    map_level = PG_LEVEL_PDP;
    ASSERT(is_aligned(vaddr, SIZE_1GB) && "hugepage must be aligned");
  }

  uint16_t table_pg_flags = PE_WRITE | PE_PRESENT;
  if (vm_flags & VM_USER || vaddr < USER_SPACE_END) {
    table_pg_flags |= PE_USER;
  }

  uint16_t entry_flags = vm_flags_to_pe_flags(vm_flags);
  for (pg_level_t level = PG_LEVEL_PML4; level > map_level; level--) {
    uint64_t *table = get_pgtable_address(vaddr, level);
    int index = index_for_pg_level(vaddr, level);
    uintptr_t next_table = table[index] & PE_FRAME_MASK;
    if (next_table == 0) {
      // create new table
      page_t *table_page = alloc_pages(1);
      SLIST_ADD(&table_pages, table_page, next);
      table[index] = table_page->address | table_pg_flags;
      uint64_t *new_table = get_pgtable_address(vaddr, level - 1);
      memset(new_table, 0, PAGE_SIZE);
    } else {
      table[index] = next_table | table_pg_flags;
    }
  }

  int index = index_for_pg_level(vaddr, map_level);
  uint64_t *table = get_pgtable_address(vaddr, map_level);
  table[index] = paddr | entry_flags;
  if (out_pages != NULL) {
    *out_pages = LIST_FIRST(&table_pages);
  }
  cpu_invlpg(vaddr);
  return table + index;
}

void recursive_unmap_entry(uintptr_t vaddr, uint32_t vm_flags) {
  pg_level_t map_level = PG_LEVEL_PT;
  pg_level_t level = PG_LEVEL_PT;
  if (vm_flags & VM_HUGE_2MB) {
    level = PG_LEVEL_PD;
  } else if (vm_flags & VM_HUGE_1GB) {
    level = PG_LEVEL_PDP;
  }

  int index = index_for_pg_level(vaddr, level);
  get_pgtable_address(vaddr, level)[index] = 0;
  cpu_invlpg(vaddr);
}

void recursive_update_entry_flags(uintptr_t vaddr, uint32_t vm_flags) {
  pg_level_t level = PG_LEVEL_PT;
  if (vm_flags & VM_HUGE_2MB) {
    level = PG_LEVEL_PD;
  } else if (vm_flags & VM_HUGE_1GB) {
    level = PG_LEVEL_PDP;
  }

  int index = index_for_pg_level(vaddr, level);
  uint64_t *pt = get_pgtable_address(vaddr, level);
  cpu_invlpg(pt);
  pt[index] = (pt[index] & PE_FRAME_MASK) | vm_flags_to_pe_flags(vm_flags);
  cpu_invlpg(vaddr);
}

void recursive_update_entry_entry(uintptr_t vaddr, uintptr_t frame, uint32_t vm_flags) {
  pg_level_t level = PG_LEVEL_PT;
  if (vm_flags & VM_HUGE_2MB) {
    level = PG_LEVEL_PD;
  } else if (vm_flags & VM_HUGE_1GB) {
    level = PG_LEVEL_PDP;
  }

  int index = index_for_pg_level(vaddr, level);
  uint64_t *pt = get_pgtable_address(vaddr, level);
  cpu_invlpg(pt);
  pt[index] = (frame & PE_FRAME_MASK) | vm_flags_to_pe_flags(vm_flags);
  cpu_invlpg(vaddr);
}

uint64_t recursive_duplicate_pgtable(
  const uint64_t *src_table,
  pg_level_t level,
  uintptr_t virt_addr,
  uint16_t index,
  page_t **out_pages
) {
  if (level == PG_LEVEL_PT) {
    // this is the last level, copy the entry directly
    return src_table[index] & ~(PE_ACCESSED | PE_DIRTY);
  } else if ((src_table[index] & PE_PRESENT) == 0) {
    return 0;
  }

  // allocate a new page for dest_table[index]
  LIST_HEAD(page_t) table_pages = {0};
  page_t *dest_page = alloc_pages(1);
  SLIST_ADD(&table_pages, dest_page, next);

  // save the old temporary mapping and map the new page
  uint64_t old_temp_pdpte = *TEMP_PDPTE;
  *TEMP_PDPTE = dest_page->address | PE_WRITE | PE_PRESENT;
  cpu_invlpg(TEMP_PTR);

  uint64_t *next_src_table = get_pgtable_address(virt_addr, level-1);
  // kprintf("recursive_duplicate_pgtable: level=%d, index=%d, virt_addr=%p, next_src_table=%p\n",
  //         level, index, virt_addr, next_src_table);
  for (int i = 0; i < NUM_ENTRIES; i++) {
    uintptr_t next_virt_addr = virt_addr | ((uint64_t) i << pg_level_to_shift(level-1));
    // if (next_src_table[i] != 0)
    //   kprintf("  next_virt_addr=%p, src_table[%d]=%p, level=%d (%p, %p)\n",
    //           next_virt_addr, i, next_src_table[i], level-1, virt_addr, ((uint64_t)i << pg_level_to_shift(level-1)));
    page_t *page_ptr = NULL;
    TEMP_PTR[i] = recursive_duplicate_pgtable(next_src_table, level - 1, next_virt_addr, i, &page_ptr);
    if (page_ptr != NULL) {
      SLIST_ADD_SLIST(&table_pages, page_ptr, SLIST_GET_LAST(page_ptr, next), next);
    }
  }

  if (out_pages != NULL) {
    *out_pages = LIST_FIRST(&table_pages);
  }

  *TEMP_PDPTE = old_temp_pdpte;
  cpu_invlpg(TEMP_PTR);

  uint16_t flags = src_table[index] & PE_FLAGS_MASK;
  flags &= ~(PE_ACCESSED | PE_DIRTY);
  return dest_page->address | flags;
}

//

void fill_unmapped_page(page_t *page, uint8_t v, size_t off, size_t len) {
  ASSERT(off + len <= PAGE_SIZE);
  critical_enter();
  // ---------------------
  *TEMP_PDPTE = page->address | PE_WRITE | PE_PRESENT;
  cpu_invlpg(TEMP_PTR);
  memset(((void *)TEMP_PTR) + off, v, len);
  *TEMP_PDPTE = 0;
  // ---------------------
  critical_exit();
}

void fill_unmapped_pages(page_t *pages, uint8_t v, size_t off, size_t len) {
  size_t pgsize = pg_flags_to_size(pages->flags);
  ASSERT(pgsize == PAGE_SIZE);
  if (pages->flags & PG_HEAD) {
    // the offset and length can span the whole page list
    size_t list_size = PAGES_TO_SIZE(pages->head.count);
    ASSERT(off + len <= list_size);

    bool contiguous = pages->head.contiguous;
    page_t *page = pages;
    while (page && off >= pgsize) {
      // get to the page for the offset
      off -= pgsize;
      page = page->next;
    }

    // fill the pages
    while (page && len > 0) {
      size_t n = min(pgsize - off, len);
      fill_unmapped_page(page, v, off, n);
      len -= n;
      off = 0;
      page = page->next;
    }
  } else {
    // we are dealing with only a single page
    ASSERT(off + len <= PAGE_SIZE);
    fill_unmapped_page(pages, v, off, len);
  }
}

size_t rw_unmapped_page(page_t *page, size_t off, kio_t *kio) {
  void *tmp_ptr = TEMP_PTR;
  size_t pgsize = pg_flags_to_size(page->flags);
  ASSERT(pgsize == PAGE_SIZE); // TODO: support big pages
  ASSERT(off < pgsize);

  size_t len = min(pgsize - off, kio_remaining(kio));
  size_t n = 0;
  critical_enter();
  // ---------------------
  *TEMP_PDPTE = page->address | PE_WRITE | PE_PRESENT;
  cpu_invlpg(TEMP_PTR);
  if (kio->dir == KIO_WRITE) {
    n = kio_write_in(kio, offset_ptr(TEMP_PTR, off), len, 0);
  } else if (kio->dir == KIO_READ) {
    n = kio_read_out(offset_ptr(TEMP_PTR, off), len, 0, kio);
  }
  *TEMP_PDPTE = 0;
  // ---------------------
  critical_exit();
  return n;
}

size_t rw_unmapped_pages(page_t *pages, size_t off, kio_t *kio) {
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

//

void nonrecursive_map_frames(uintptr_t pml4, uintptr_t vaddr, uintptr_t paddr, size_t count, uint32_t vm_flags, __move page_t **out_pages) {
  ASSERT(vaddr < USER_SPACE_END);
  ASSERT(is_aligned(vaddr, PAGE_SIZE));
  ASSERT(is_aligned(paddr, PAGE_SIZE));

  pg_level_t map_level = vm_flags_to_level(vm_flags);
  uint16_t pe_flags = vm_flags_to_pe_flags(vm_flags);
  LIST_HEAD(page_t) table_pages = {0};
  while (count > 0) {
    critical_enter();

    // walk down the hierarchy by mapping in one level at a time
    *TEMP_PDPTE = pml4 | PE_GLOBAL | PE_WRITE | PE_PRESENT;
    cpu_invlpg(TEMP_PTR);

    for (pg_level_t level = PG_LEVEL_PML4; level > map_level; level--) {
      int index = index_for_pg_level(vaddr, level);
      uintptr_t next_table = TEMP_PTR[index] & PE_FRAME_MASK;
      if (next_table == 0) {
        page_t *table_page = alloc_pages(1);
        SLIST_ADD(&table_pages, table_page, next);
        TEMP_PTR[index] = table_page->address | PE_USER | PE_WRITE | PE_PRESENT;

        // update temp pdpe entry to point to the new table
        *TEMP_PDPTE = table_page->address | PE_GLOBAL | PE_WRITE | PE_PRESENT;
        cpu_invlpg(TEMP_PTR);
        memset(TEMP_PTR, 0, PAGE_SIZE);
      } else {
        TEMP_PTR[index] = next_table | PE_USER | PE_WRITE | PE_PRESENT;

        // update temp pdpe entry to point to the existing table
        *TEMP_PDPTE = next_table | PE_WRITE_THROUGH | PE_WRITE | PE_PRESENT;
        cpu_invlpg(TEMP_PTR);
      }
    }

    // now `TEMP_PTR` points to the level where the given frames should be mapped
    volatile uint64_t *table = TEMP_PTR;
    int index = index_for_pg_level(vaddr, map_level);
    for (int i = index; i < NUM_ENTRIES && count > 0; i++) {
      table[i] = paddr | pe_flags;
      vaddr += PAGE_SIZE;
      paddr += PAGE_SIZE;
      count--;
    }

    *TEMP_PDPTE = 0;
    cpu_invlpg(TEMP_PTR);

    // if mapping across multiple tables exit critical in-between to allow other
    // threads to preempt if necessary
    critical_exit();
  }
  *out_pages = LIST_FIRST(&table_pages);
}

void nonrecursive_map_pages(uintptr_t pml4, uintptr_t vaddr, page_t *pages, uint32_t vm_flags, __move page_t **out_pages) {
  ASSERT(vaddr < USER_SPACE_END);
  ASSERT(is_aligned(vaddr, PAGE_SIZE));
  ASSERT(pages->flags & PG_HEAD);

  if (pages->head.contiguous) {
    nonrecursive_map_frames(pml4, vaddr, pages->address, pages->head.count, vm_flags, out_pages);
    return;
  }

  size_t pg_size = vm_flags_to_size(vm_flags);
  page_t *page = pages;
  LIST_HEAD(page_t) table_pages = {0};
  while (page != NULL) {
    uintptr_t paddr = page->address;

    page_t *tmp_table_pages = NULL;
    nonrecursive_map_frames(pml4, vaddr, paddr, 1, vm_flags, &tmp_table_pages);
    vaddr += pg_size;
    if (tmp_table_pages != NULL) {
      SLIST_ADD_SLIST(&table_pages, tmp_table_pages, SLIST_GET_LAST(tmp_table_pages, next), next);
    }

    page = page->next;
  }

  *out_pages = LIST_FIRST(&table_pages);
}

//
//

uintptr_t create_new_ap_page_tables(__move page_t **out_pages) {
  LIST_HEAD(page_t) table_pages = {0};
  page_t *new_pml4 = alloc_pages(1);
  SLIST_ADD(&table_pages, new_pml4, next);

  critical_enter();
  *TEMP_PDPTE = new_pml4->address | PE_WRITE | PE_PRESENT;
  uint64_t *table_virt = TEMP_PTR;
  memset(table_virt, 0, PAGE_SIZE);

  // shallow copy kernel entries
  for (int i = PML4_INDEX(KERNEL_SPACE_START); i <= PML4_INDEX(KERNEL_SPACE_END); i++) {
    if (i == R_ENTRY) {
      table_virt[i] = new_pml4->address | PE_WRITE | PE_PRESENT;
    } else {
      table_virt[i] = PML4_PTR[i];
    }
  }

  // identity map bottom of memory
  page_t *new_low_pdpt = alloc_pages(1);
  SLIST_ADD(&table_pages, new_low_pdpt, next);
  table_virt[0] = new_low_pdpt->address | PE_WRITE | PE_PRESENT; // pml4 -> pdpe

  *TEMP_PDPTE = new_low_pdpt->address | PE_WRITE | PE_PRESENT;
  cpu_invlpg(TEMP_PTR);
  uint64_t *low_pdpt = TEMP_PTR;
  memset(low_pdpt, 0, PAGE_SIZE);

  page_t *new_low_pde = alloc_pages(1);
  SLIST_ADD(&table_pages, new_low_pde, next);
  low_pdpt[0] = new_low_pde->address | PE_WRITE | PE_PRESENT; // pdpe -> pde

  *TEMP_PDPTE = new_low_pde->address | PE_WRITE | PE_PRESENT;
  cpu_invlpg(TEMP_PTR);
  uint64_t *low_pde = TEMP_PTR;
  memset(low_pde, 0, PAGE_SIZE);
  low_pde[0] = 0 | PE_SIZE | PE_WRITE | PE_PRESENT; // pde -> 2mb identity mapping

  *TEMP_PDPTE = 0;
  cpu_invlpg(TEMP_PTR);
  critical_exit();

  if (out_pages != NULL) {
    *out_pages = LIST_FIRST(&table_pages);
  }
  return new_pml4->address;
}

uintptr_t fork_page_tables(page_t **out_pages, bool fork_user) {
  LIST_HEAD(page_t) table_pages = {0};
  page_t *new_pml4 = alloc_pages(1);
  SLIST_ADD(&table_pages, new_pml4, next);

  critical_enter();
  *TEMP_PDPTE = new_pml4->address | PE_WRITE | PE_PRESENT;
  cpu_invlpg(TEMP_PTR);
  uint64_t *table_virt = TEMP_PTR;
  memset(table_virt, 0, PAGE_SIZE);

  // shallow copy kernel entries
  for (int i = PML4_INDEX(KERNEL_SPACE_START); i <= PML4_INDEX(KERNEL_SPACE_END); i++) {
    if (i == R_ENTRY) {
      table_virt[i] = new_pml4->address | PE_WRITE | PE_PRESENT;
    } else {
      table_virt[i] = PML4_PTR[i];
    }
  }

  // fork user entries if requested
  if (fork_user) {
    for (int i = PML4_INDEX(USER_SPACE_START); i <= PML4_INDEX(USER_SPACE_END); i++) {
      uintptr_t vaddr = ((uint64_t)i << pg_level_to_shift(PG_LEVEL_PML4));
      page_t *page_ptr = NULL;
      table_virt[i] = recursive_duplicate_pgtable(PML4_PTR, PG_LEVEL_PML4, vaddr, i, &page_ptr);
      if (page_ptr != NULL) {
        SLIST_ADD_SLIST(&table_pages, page_ptr, SLIST_GET_LAST(page_ptr, next), next);
      }
    }
  }

  *TEMP_PDPTE = 0;
  cpu_invlpg(TEMP_PTR);
  critical_exit();

  if (out_pages != NULL) {
    *out_pages = LIST_FIRST(&table_pages);
  }
  return new_pml4->address;
}

//

void pgtable_print_debug_table(
  uintptr_t table_phys,
  uintptr_t virt_addr,
  pg_level_t level,
  pg_level_t min_level,
  int start_bound,
  int end_bound,
  int min_bound_level
) {
  ASSERT(is_aligned(table_phys, PAGE_SIZE));
  ASSERT(start_bound >= 0 && start_bound < NUM_ENTRIES && start_bound < end_bound);
  ASSERT(end_bound >= start_bound && end_bound <= NUM_ENTRIES);
  ASSERT(min_bound_level >= 0);
  if (level < min_level) {
    return;
  }
  unsigned int indent = (PG_LEVEL_PML4 - level) * 2;

  critical_enter();
  uint64_t old_temp_pdpte = *TEMP_PDPTE;
  *TEMP_PDPTE = table_phys | PE_GLOBAL | PE_WRITE | PE_PRESENT;

  cpu_invlpg(TEMP_PTR);
  for (int i = start_bound; i < end_bound; i++) {
    uint64_t entry = TEMP_PTR[i];
    if ((entry & PE_PRESENT) == 0) {
      continue;
    }

    uintptr_t entry_frame = entry & PE_FRAME_MASK;
    uintptr_t entry_vaddr = virt_addr | ((uint64_t)i << pg_level_to_shift(level));
    if ((entry_vaddr & (1ULL << 47)) != 0) {
      // sign extend the address
      entry_vaddr |= 0xffff000000000000ULL;
    }

    uintptr_t vspace_mapped = 0;
    const char *level_name = "PML4";
    if (level == PG_LEVEL_PDP) {
      level_name = "PDP";
      if (entry & PE_SIZE)
        vspace_mapped = SIZE_1GB;
    } else if (level == PG_LEVEL_PD) {
      level_name = "PD";
      if (entry & PE_SIZE)
        vspace_mapped = SIZE_2MB;
    } else if (level == PG_LEVEL_PT) {
      level_name = "PT";
      vspace_mapped = PAGE_SIZE;
    }

    kprintf("{:$ <*}%s[%03d] ", indent, level_name, i);
    kprintf(
      "%c%c%c%c%c%c%c%c%c",
      (entry & PE_WRITE) ? 'W' : '-',
      (entry & PE_USER) ? 'U' : '-',
      (entry & PE_WRITE_THROUGH) ? 'T' : '-',
      (entry & PE_CACHE_DISABLE) ? 'C' : '-',
      (entry & PE_ACCESSED) ? 'A' : '-',
      (entry & PE_DIRTY) ? 'D' : '-',
      (entry & PE_SIZE) ? 'S' : '-',
      (entry & PE_GLOBAL) ? 'G' : '-',
      (entry & PE_NO_EXECUTE) ? '-' : 'X'
    );
    if (vspace_mapped > 0) {
      kprintf(" %M", vspace_mapped);
    }
    kprintf(" -> %018p\n", entry_vaddr);

    if (level > min_level) {
      if (vspace_mapped == 0) {
        int start = level >= min_bound_level ? 0 : start_bound;
        int end = level >= min_bound_level ? NUM_ENTRIES-1 : end_bound;
        pgtable_print_debug_table(entry_frame, entry_vaddr, level - 1, min_level, start, end, min_bound_level);
      }
    }
  }
  *TEMP_PDPTE = old_temp_pdpte;
  cpu_invlpg(TEMP_PTR);
  critical_exit();
}

void pgtable_print_debug_pml4(uintptr_t pml4_phys, int max_depth, int start_bound, int end_bound, int bound_level) {
  ASSERT(is_aligned(pml4_phys, PAGE_SIZE));
  pg_level_t min_level;
  switch (max_depth) {
    case 0: min_level = PG_LEVEL_PML4; break;
    case 1: min_level = PG_LEVEL_PDP; break;
    case 2: min_level = PG_LEVEL_PD; break;
    default: min_level = PG_LEVEL_PT; break;
  }

  end_bound = end_bound < 0 ? NUM_ENTRIES : end_bound;
  start_bound = min(max(start_bound, 0), end_bound - 1);
  bound_level = min(bound_level < 0 ? 0 : bound_level, PG_LEVEL_PML4);
  pgtable_print_debug_table(pml4_phys, 0x0, PG_LEVEL_PML4, min_level, start_bound, end_bound, bound_level);
}


void pgtable_print_debug_pml4_user(uintptr_t pml4_phys) {
  ASSERT(is_aligned(pml4_phys, PAGE_SIZE));
  pgtable_print_debug_table(pml4_phys, 0x0, PG_LEVEL_PML4, PG_LEVEL_PT, 0, 256, PG_LEVEL_PML4);
}

void _print_pgtable_indexes(uintptr_t addr) {
  kprintf(
    "[pgtable] %018p -> pml4[%d][%d][%d][%d]\n",
    addr,
    PML4_INDEX(addr), PDPT_INDEX(addr),
    PDT_INDEX(addr), PT_INDEX(addr));
}

void _print_pgtable_address(uint16_t l4, uint16_t l3, uint16_t l2, uint16_t l1) {
  kprintf(
    "[pgtable] pml4[%d][%d][%d][%d] -> %018p\n",
    l4, l3, l2, l1, get_virt_addr(l4, l3, l2, l1));
}
