//
// Created by Aaron Gill-Braun on 2022-06-18.
//

#include <mm/pgtable.h>
#include <mm/vmalloc.h>
#include <mm/pmalloc.h>
#include <mm/init.h>
#include <mm_types.h>

#include <cpu/cpu.h>
#include <string.h>
#include <printf.h>
#include <panic.h>

#define get_virt_addr(l4, l3, l2, l1) ((0xFFFFULL << 48) | ((uint64_t)(l4) << 39) | \
  ((uint64_t)(l3) << 30) | ((uint64_t)(l2) << 21) | ((uint64_t)(l1) << 12))
#define index_for_pg_level(addr, level) (((addr) >> (12 + ((level) * 9))) & 0x1FF)
#define pg_level_to_shift(level) (12 + ((level) * 9))

#define PT_INDEX(a) (((a) >> 12) & 0x1FF)
#define PDT_INDEX(a) (((a) >> 21) & 0x1FF)
#define PDPT_INDEX(a) (((a) >> 30) & 0x1FF)
#define PML4_INDEX(a) (((a) >> 39) & 0x1FF)

#define U_ENTRY 0ULL
#define R_ENTRY 510ULL
#define K_ENTRY 511ULL
#define T_ENTRY 509ULL

#define PML4_PTR ((uint64_t *) get_virt_addr(R_ENTRY, R_ENTRY, R_ENTRY, R_ENTRY))
#define TEMP_PTR ((uint64_t *) get_virt_addr(R_ENTRY, R_ENTRY, R_ENTRY, T_ENTRY))

// page entry flags
#define PE_PRESENT        (1ULL << 0)
#define PE_WRITE          (1ULL << 1)
#define PE_USER           (1ULL << 2)
#define PE_WRITE_THROUGH  (1ULL << 3)
#define PE_CACHE_DISABLE  (1ULL << 4)
#define PE_SIZE           (1ULL << 7)
#define PE_GLOBAL         (1ULL << 8)
#define PE_NO_EXECUTE     (1ULL << 63)

typedef enum pg_level {
  PG_LEVEL_PT,
  PG_LEVEL_PD,
  PG_LEVEL_PDP,
  PG_LEVEL_PML4,
  PG_LEVEL_MAX
} pg_level_t;

void _print_pgtable_indexes(uintptr_t addr);
void _print_pgtable_address(uint16_t l4, uint16_t l3, uint16_t l2, uint16_t l1);

// kernel page table
uint64_t *early_kernel_pgtable;

uint64_t *get_child_pgtable_address(const uint64_t *parent, pg_level_t level, uint16_t index) {
  uintptr_t addr = (uintptr_t) parent;
  addr |= (index << pg_level_to_shift(level));
  return (uint64_t *) addr;
}

uint64_t *get_pgtable_address(uintptr_t virt_addr, pg_level_t level) {
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

uint16_t page_to_entry_flags(uint32_t flags) {
  uint16_t entry_flags = PE_PRESENT;
  entry_flags |= (flags & PG_WRITE) ? PE_WRITE : 0;
  entry_flags |= (flags & PG_USER) ? PE_USER : 0;
  entry_flags |= (flags & PG_NOCACHE) ? PE_CACHE_DISABLE : 0;
  entry_flags |= (flags & PG_WRITETHRU) ? PE_WRITE_THROUGH : 0;
  entry_flags |= ((flags & PG_BIGPAGE) || (flags & PG_HUGEPAGE)) ? PE_SIZE : 0;
  entry_flags |= (flags & PG_GLOBAL) ? PE_GLOBAL : 0;
  entry_flags |= (flags & PG_EXEC) ? 0 : PE_NO_EXECUTE;
  return entry_flags;
}

//

void early_init_pgtable() {
  early_kernel_pgtable = (void *) get_current_pgtable();
}

uint64_t *early_map_entry(uintptr_t virt_addr, uintptr_t phys_addr, uint32_t flags) {
  kassert(virt_addr % PAGE_SIZE == 0);
  kassert(phys_addr % PAGE_SIZE == 0);

  pg_level_t level = PG_LEVEL_PT;
  if (flags & PG_BIGPAGE) {
    kassert(is_aligned(virt_addr, SIZE_2MB));
    kassert(is_aligned(phys_addr, SIZE_2MB));
    level = PG_LEVEL_PD;
  } else if (flags & PG_HUGEPAGE) {
    kassert(is_aligned(virt_addr, SIZE_1GB));
    kassert(is_aligned(phys_addr, SIZE_1GB));
    level = PG_LEVEL_PDP;
  }

  uint16_t entry_flags = page_to_entry_flags(flags);
  uint64_t *pml4 = (void *) ((uint64_t) boot_info_v2->pml4_addr);
  uint64_t *table = pml4;
  for (pg_level_t i = PG_LEVEL_PML4; i > level; i--) {
    int index = index_for_pg_level(virt_addr, i);
    uintptr_t next_table = table[index] & PAGE_FRAME_MASK;
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

  int index = index_for_pg_level(virt_addr, level);
  table[index] = phys_addr | entry_flags;
  return table + index;
}

void *early_map_entries(uintptr_t virt_addr, uintptr_t phys_addr, size_t count, uint32_t flags) {
  kassert(virt_addr % PAGE_SIZE == 0);
  kassert(phys_addr % PAGE_SIZE == 0);
  kassert(count > 0);

  pg_level_t level = PG_LEVEL_PT;
  size_t stride = PAGE_SIZE;
  if (flags & PG_BIGPAGE) {
    kassert(is_aligned(virt_addr, SIZE_2MB));
    kassert(is_aligned(phys_addr, SIZE_2MB));
    level = PG_LEVEL_PD;
    stride = SIZE_2MB;
  } else if (flags & PG_HUGEPAGE) {
    kassert(is_aligned(virt_addr, SIZE_1GB));
    kassert(is_aligned(phys_addr, SIZE_1GB));
    level = PG_LEVEL_PDP;
    stride = SIZE_1GB;
  }

  void *addr = (void *) virt_addr;
  uint16_t entry_flags = page_to_entry_flags(flags);
  while (count > 0) {
    int index = index_for_pg_level(virt_addr, level);
    uint64_t *entry = early_map_entry(virt_addr, phys_addr, flags);
    entry++;
    count--;
    virt_addr += stride;
    phys_addr += stride;

    for (int i = index + 1; i < 512; i++) {
      if (count == 0) {
        break;
      }

      *entry = phys_addr | entry_flags;
      entry++;
      count--;
      virt_addr += stride;
      phys_addr += stride;
    }
  }

  cpu_flush_tlb();
  return addr;
}

//

void init_recursive_pgtable(uint64_t *table_virt, uintptr_t table_phys) {
  for (int i = PML4_INDEX(KERNEL_SPACE_START); i < PML4_INDEX(KERNEL_SPACE_END); i++) {
    if (i == R_ENTRY) {
      table_virt[i] = (uint64_t) table_phys | PE_WRITE | PE_PRESENT;
    } else {
      table_virt[i] = early_kernel_pgtable[i];
    }
  }
  cpu_flush_tlb();
}

uint64_t *recursive_map_entry(uintptr_t virt_addr, uintptr_t phys_addr, uint32_t flags, page_t **out_pages) {
  LIST_HEAD(page_t) table_pages = LIST_HEAD_INITR;
  pg_level_t level = PG_LEVEL_PT;
  if (flags & PG_BIGPAGE) {
    level = PG_LEVEL_PD;
  } else if (flags & PG_HUGEPAGE) {
    level = PG_LEVEL_PDP;
  }

  uint16_t entry_flags = page_to_entry_flags(flags);
  for (pg_level_t i = PG_LEVEL_PML4; i > level; i--) {
    uint64_t *table = get_pgtable_address(virt_addr, i);
    int index = index_for_pg_level(virt_addr, i);
    uintptr_t next_table = table[index] & PAGE_FRAME_MASK;
    if (next_table == 0) {
      // create new table
      page_t *table_page = _alloc_pages(1, 0);
      SLIST_ADD(&table_pages, table_page, next);
      table[index] = table_page->address | PE_WRITE | PE_PRESENT;
      uint64_t *new_table = get_pgtable_address(virt_addr, i - 1);
      memset(new_table, 0, PAGE_SIZE);
    } else if (!(table[index] & PE_PRESENT)) {
      table[index] = next_table | PE_WRITE | PE_PRESENT;
    }
  }

  int index = index_for_pg_level(virt_addr, level);
  uint64_t *table = get_pgtable_address(virt_addr, level);
  table[index] = phys_addr | entry_flags;
  if (out_pages != NULL) {
    *out_pages = LIST_FIRST(&table_pages);
  }
  return table + index;
}

void recursive_unmap_entry(uintptr_t virt_addr, uint32_t flags) {
  flags &= PAGE_FLAGS_MASK;
  pg_level_t level = PG_LEVEL_PT;
  if (flags & PG_BIGPAGE) {
    level = PG_LEVEL_PD;
  } else if (flags & PG_HUGEPAGE) {
    level = PG_LEVEL_PDP;
  }

  int index = index_for_pg_level(virt_addr, level);
  get_pgtable_address(virt_addr, level)[index] = 0;
  cpu_flush_tlb();
}

uint64_t recursive_duplicate_pgtable(
  pg_level_t level,
  uint64_t *dest_parent_table,
  uint64_t *src_parent_table,
  uint16_t index,
  page_t **out_pages
) {
  if (level == PG_LEVEL_PT) {
    return src_parent_table[index];
  } else if ((src_parent_table[index] & PE_PRESENT) == 0) {
    return 0;
  }

  LIST_HEAD(page_t) table_pages = LIST_HEAD_INITR;
  page_t *dest_page = _alloc_pages(1, 0);
  dest_parent_table[index] = dest_page->address | PE_WRITE | PE_PRESENT;
  SLIST_ADD(&table_pages, dest_page, next);

  uint64_t *src_table = get_child_pgtable_address(src_parent_table, index, level);
  uint64_t *dest_table = get_child_pgtable_address(dest_parent_table, index, level);
  for (int i = 0; i < 512; i++) {
    page_t *page_ptr = NULL;
    dest_table[i] = recursive_duplicate_pgtable(level - 1, dest_table, src_table, i, &page_ptr);
    if (page_ptr != NULL) {
      SLIST_ADD_SLIST(&table_pages, page_ptr, SLIST_GET_LAST(page_ptr, next), next);
    }
  }

  if (out_pages != NULL) {
    *out_pages = LIST_FIRST(&table_pages);
  }

  uint16_t flags = src_parent_table[index] & PAGE_FLAGS_MASK;
  return dest_page->address | flags;
}

//

uintptr_t get_current_pgtable() {
  return ((uint64_t) __read_cr3()) & PAGE_FRAME_MASK;
}

void set_current_pgtable(uintptr_t table_phys) {
  __write_cr3((uint64_t) table_phys);
  cpu_flush_tlb();
}

size_t pg_flags_to_size(uint32_t flags) {
  if (flags & PG_HUGEPAGE) {
    // fallback to big pages if huge pages are not supported
    return cpu_query_feature(CPU_BIT_PDPE1GB) ? SIZE_1GB : SIZE_2MB;
  } else if (flags & PG_BIGPAGE) {
    return SIZE_2MB;
  }
  return SIZE_4KB;
}

//

uintptr_t create_new_page_tables(page_t **out_pages) {
  LIST_HEAD(page_t) table_pages = LIST_HEAD_INITR;
  page_t *new_pml4 = _alloc_pages(1, PG_WRITE);
  SLIST_ADD(&table_pages, new_pml4, next);

  PML4_PTR[T_ENTRY] = new_pml4->address | PE_WRITE | PE_PRESENT;

  uint64_t *table_virt = TEMP_PTR;
  memset(table_virt, 0, PAGE_SIZE);

  // shallow copy kernel entries
  for (int i = PML4_INDEX(KERNEL_SPACE_START); i < PML4_INDEX(KERNEL_SPACE_END); i++) {
    if (i == R_ENTRY) {
      table_virt[i] = (uint64_t) new_pml4->address | PE_WRITE | PE_PRESENT;
    } else {
      table_virt[i] = early_kernel_pgtable[i];
    }
  }

  PML4_PTR[T_ENTRY] = 0;
  if (out_pages != NULL) {
    *out_pages = LIST_FIRST(&table_pages);
  }
  return new_pml4->address;
}

uintptr_t deepcopy_fork_page_tables(page_t **out_pages) {
  LIST_HEAD(page_t) table_pages = LIST_HEAD_INITR;
  page_t *new_pml4 = _alloc_pages(1, PG_WRITE);
  SLIST_ADD(&table_pages, new_pml4, next);

  PML4_PTR[T_ENTRY] = new_pml4->address | PE_WRITE | PE_PRESENT;

  uint64_t *table_virt = TEMP_PTR;
  memset(table_virt, 0, PAGE_SIZE);

  // shallow copy kernel entries
  for (int i = PML4_INDEX(KERNEL_SPACE_START); i < PML4_INDEX(KERNEL_SPACE_END); i++) {
    if (i == T_ENTRY)
      continue;

    if (i == R_ENTRY) {
      table_virt[i] = (uint64_t) new_pml4->address | PE_WRITE | PE_PRESENT;
    } else {
      table_virt[i] = early_kernel_pgtable[i];
    }
  }

  // deep copy user entries
  for (int i = PML4_INDEX(USER_SPACE_START); i < PML4_INDEX(USER_SPACE_END); i++) {
    page_t *page_ptr = NULL;
    uint64_t *src_table = (void *) get_virt_addr(R_ENTRY, R_ENTRY, R_ENTRY, i);
    uint64_t *dest_table = TEMP_PTR;
    TEMP_PTR[i] = recursive_duplicate_pgtable(PG_LEVEL_PDP, src_table, dest_table, i, &page_ptr);
    if (page_ptr != NULL) {
      SLIST_ADD_SLIST(&table_pages, page_ptr, SLIST_GET_LAST(page_ptr, next), next);
    }
  }

  PML4_PTR[T_ENTRY] = 0;
  if (out_pages != NULL) {
    *out_pages = LIST_FIRST(&table_pages);
  }
  return new_pml4->address;
}

//

void _print_pgtable_indexes(uintptr_t addr) {
  kprintf("[pgtable] %018p -> pml4[%d][%d][%d][%d]\n",
          addr,
          PML4_INDEX(addr), PDPT_INDEX(addr),
          PDT_INDEX(addr), PT_INDEX(addr));
}

void _print_pgtable_address(uint16_t l4, uint16_t l3, uint16_t l2, uint16_t l1) {
  kprintf("[pgtable] pml4[%d][%d][%d][%d] -> %018p\n",
          l4, l3, l2, l1, get_virt_addr(l4, l3, l2, l1));
}
