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
  PG_LEVEL_MAX
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
  return entry_flags;
}

//

uint64_t *early_map_entry(uintptr_t virt_addr, uintptr_t phys_addr, uint32_t vm_flags) {
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

void *early_map_entries(uintptr_t virt_addr, uintptr_t phys_addr, size_t count, uint32_t vm_flags) {
  ASSERT(virt_addr % PAGE_SIZE == 0);
  ASSERT(phys_addr % PAGE_SIZE == 0);
  ASSERT(count > 0);

  pg_level_t map_level = PG_LEVEL_PT;
  size_t stride = PAGE_SIZE;
  if (vm_flags & VM_HUGE_2MB) {
    ASSERT(is_aligned(virt_addr, SIZE_2MB));
    ASSERT(is_aligned(phys_addr, SIZE_2MB));
    map_level = PG_LEVEL_PD;
    stride = SIZE_2MB;
  } else if (vm_flags & VM_HUGE_1GB) {
    ASSERT(is_aligned(virt_addr, SIZE_1GB));
    ASSERT(is_aligned(phys_addr, SIZE_1GB));
    map_level = PG_LEVEL_PDP;
    stride = SIZE_1GB;
  }

  void *addr = (void *) virt_addr;
  uint16_t entry_flags = vm_flags_to_pe_flags(vm_flags);
  while (count > 0) {
    int index = index_for_pg_level(virt_addr, map_level);
    uint64_t *entry = early_map_entry(virt_addr, phys_addr, vm_flags);
    entry++;
    count--;
    virt_addr += stride;
    phys_addr += stride;

    for (int i = index + 1; i < NUM_ENTRIES; i++) {
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

uintptr_t clone_init_pgtable() {
  page_t *newtable_page = alloc_pages(1);
  *TEMP_PDPTE = newtable_page->address | PE_WRITE | PE_PRESENT;
  uint64_t *newtable_virt = TEMP_PTR;
  memcpy(newtable_virt, startup_kernel_pml4, PAGE_SIZE);
  *TEMP_PDPTE = 0;
  return newtable_page->address;
}

void pgtable_unmap_user_mappings() {
  for (int i = PML4_INDEX(USER_SPACE_START); i < PML4_INDEX(USER_SPACE_END); i++) {
    PML4_PTR[i] = 0;
  }
}

uint64_t *recursive_map_entry(uintptr_t virt_addr, uintptr_t phys_addr, uint32_t vm_flags, __move page_t **out_pages) {
  LIST_HEAD(page_t) table_pages = LIST_HEAD_INITR;
  pg_level_t map_level = PG_LEVEL_PT;
  if (vm_flags & VM_HUGE_2MB) {
    map_level = PG_LEVEL_PD;
    ASSERT(is_aligned(virt_addr, SIZE_2MB) && "bigpage must be aligned");
  } else if (vm_flags & VM_HUGE_1GB) {
    map_level = PG_LEVEL_PDP;
    ASSERT(is_aligned(virt_addr, SIZE_1GB) && "hugepage must be aligned");
  }

  uint16_t table_pg_flags = PE_WRITE | PE_PRESENT;
  if (virt_addr < USER_SPACE_END) {
    table_pg_flags |= PE_USER;
  }

  uint16_t entry_flags = vm_flags_to_pe_flags(vm_flags);
  for (pg_level_t level = PG_LEVEL_PML4; level > map_level; level--) {
    uint64_t *table = get_pgtable_address(virt_addr, level);
    int index = index_for_pg_level(virt_addr, level);
    uintptr_t next_table = table[index] & PE_FRAME_MASK;
    if (next_table == 0) {
      // create new table
      page_t *table_page = alloc_pages(1);
      SLIST_ADD(&table_pages, table_page, next);
      table[index] = table_page->address | table_pg_flags;
      uint64_t *new_table = get_pgtable_address(virt_addr, level - 1);
      memset(new_table, 0, PAGE_SIZE);
    } else if (!(table[index] & PE_PRESENT)) {
      table[index] = next_table | table_pg_flags;
    }
  }

  int index = index_for_pg_level(virt_addr, map_level);
  uint64_t *table = get_pgtable_address(virt_addr, map_level);
  table[index] = phys_addr | entry_flags;
  if (out_pages != NULL) {
    *out_pages = LIST_FIRST(&table_pages);
  }
  return table + index;
}

void recursive_unmap_entry(uintptr_t virt_addr, uint32_t vm_flags) {
  pg_level_t map_level = PG_LEVEL_PT;
  pg_level_t level = PG_LEVEL_PT;
  if (vm_flags & VM_HUGE_2MB) {
    level = PG_LEVEL_PD;
  } else if (vm_flags & VM_HUGE_1GB) {
    level = PG_LEVEL_PDP;
  }

  int index = index_for_pg_level(virt_addr, level);
  get_pgtable_address(virt_addr, level)[index] = 0;
}

void recursive_update_entry(uintptr_t virt_addr, uint32_t vm_flags) {
  pg_level_t level = PG_LEVEL_PT;
  if (vm_flags & VM_HUGE_2MB) {
    level = PG_LEVEL_PD;
  } else if (vm_flags & VM_HUGE_1GB) {
    level = PG_LEVEL_PDP;
  }

  int index = index_for_pg_level(virt_addr, level);
  uint64_t *pt = get_pgtable_address(virt_addr, level);
  pt[index] = (pt[index] & PE_FRAME_MASK) | vm_flags_to_pe_flags(vm_flags);
}

void recursive_update_range(uintptr_t virt_addr, size_t size, uint32_t vm_flags) {
  size_t pg_size = PAGE_SIZE;
  pg_level_t level = PG_LEVEL_PT;
  if (vm_flags & VM_HUGE_2MB) {
    pg_size = SIZE_2MB;
    level = PG_LEVEL_PD;
  } else if (vm_flags & VM_HUGE_1GB) {
    pg_size = SIZE_1GB;
    level = PG_LEVEL_PDP;
  }

  uintptr_t end_addr = virt_addr + size;
  while (virt_addr < end_addr) {
    int index = index_for_pg_level(virt_addr, level);
    uint64_t *pt = get_pgtable_address(virt_addr, level);
    pt[index] = (pt[index] & PE_FRAME_MASK) | vm_flags_to_pe_flags(vm_flags);
    virt_addr += pg_size;
  }
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

  LIST_HEAD(page_t) table_pages = {0};
  page_t *dest_page = alloc_pages(1);
  dest_parent_table[index] = dest_page->address | PE_WRITE | PE_PRESENT;
  SLIST_ADD(&table_pages, dest_page, next);

  uint64_t *src_table = get_child_pgtable_address(src_parent_table, index, level);
  uint64_t *dest_table = get_child_pgtable_address(dest_parent_table, index, level);
  for (int i = 0; i < NUM_ENTRIES; i++) {
    page_t *page_ptr = NULL;
    dest_table[i] = recursive_duplicate_pgtable(level - 1, dest_table, src_table, i, &page_ptr);
    if (page_ptr != NULL) {
      SLIST_ADD_SLIST(&table_pages, page_ptr, SLIST_GET_LAST(page_ptr, next), next);
    }
  }

  if (out_pages != NULL) {
    *out_pages = LIST_FIRST(&table_pages);
  }

  uint16_t flags = src_parent_table[index] & PE_FLAGS_MASK;
  return dest_page->address | flags;
}

//

uintptr_t get_current_pgtable() {
  return ((uint64_t) __read_cr3()) & PE_FRAME_MASK;
}

void set_current_pgtable(uintptr_t table_phys) {
  __write_cr3((uint64_t) table_phys);
  cpu_flush_tlb();
}

//

void fill_unmapped_page(page_t *page, uint8_t v) {
  size_t pgsize = pg_flags_to_size(page->flags);
  ASSERT(pgsize == PAGE_SIZE); // TODO: support big pages
  critical_enter();
  // ---------------------
  *TEMP_PDPTE = page->address | PE_WRITE | PE_PRESENT;
  memset(TEMP_PTR, v, pgsize);
  *TEMP_PDPTE = 0;
  cpu_invlpg(TEMP_PTR);
  // ---------------------
  critical_exit();
}

size_t rw_unmapped_page(page_t *page, size_t off, kio_t *kio) {
  size_t pgsize = pg_flags_to_size(page->flags);
  ASSERT(pgsize == PAGE_SIZE); // TODO: support big pages
  ASSERT(off < pgsize);

  size_t len = min(pgsize - off, kio_remaining(kio));
  size_t n = 0;
  critical_enter();
  // ---------------------
  *TEMP_PDPTE = page->address | PE_WRITE | PE_PRESENT;
  if (kio->dir == KIO_WRITE) {
    n = kio_write_in(kio, TEMP_PTR+off, len, 0);
  } else if (kio->dir == KIO_READ) {
    n = kio_read_out(TEMP_PTR+off, len, 0, kio);
  }
  *TEMP_PDPTE = 0;
  cpu_invlpg(TEMP_PTR);
  // ---------------------
  critical_exit();
  return n;
}

//

static inline uint64_t *temp_map_user_pgtable(uintptr_t pgtable) {
  // only invalidate TEMP_PTR if we're replacing an existing mapping
  bool invlpg = (*TEMP_PDPTE & PE_FRAME_MASK) != 0;
  *TEMP_PDPTE = pgtable | PE_USER | PE_WRITE | PE_PRESENT;

  if (invlpg)
    cpu_invlpg(TEMP_PTR);
  return TEMP_PTR;
}

void nonrecursive_map_pages(uintptr_t pml4, uintptr_t start_vaddr, uint32_t vm_flags, __ref page_t *pages, __move page_t **out_pages) {
  ASSERT(start_vaddr < USER_SPACE_END);
  ASSERT(pages->flags & PG_HEAD);

  LIST_HEAD(page_t) table_pages = LIST_HEAD_INITR;
  pg_level_t map_level = PG_LEVEL_PT;
  if (pages->flags & PG_BIGPAGE) {
    map_level = PG_LEVEL_PD;
    ASSERT(is_aligned(start_vaddr, SIZE_2MB) && "bigpage must be aligned");
  } else if (pages->flags & PG_HUGEPAGE) {
    map_level = PG_LEVEL_PDP;
    ASSERT(is_aligned(start_vaddr, SIZE_1GB) && "hugepage must be aligned");
  }

  uintptr_t virt_addr = start_vaddr;
  size_t count = pages->head.count;
  page_t *curpage = pages;
  uint16_t pe_flags = vm_flags_to_pe_flags(vm_flags);

  critical_enter();
  while (count > 0) {
    volatile uint64_t *table = temp_map_user_pgtable(pml4);
    for (pg_level_t level = PG_LEVEL_PML4; level > map_level; level--) {
      int index = index_for_pg_level(virt_addr, level);
      uintptr_t next_table = table[index] & PE_FRAME_MASK;
      if (next_table == 0) {
        page_t *table_page = alloc_pages(1);
        SLIST_ADD(&table_pages, table_page, next);
        table[index] = table_page->address | PE_USER | PE_WRITE | PE_PRESENT;

        table = temp_map_user_pgtable(table_page->address);
        memset(table, 0, PAGE_SIZE);
      } else if (!(table[index] & PE_PRESENT)) {
        table[index] = next_table | PE_USER | PE_WRITE | PE_PRESENT;
        table = temp_map_user_pgtable(next_table);
      }

      // previously temp mapped table swaped with the next
    }

    // we're at the level where the given pages should be mapped
    int start_index = index_for_pg_level(virt_addr, map_level);
    for (int i = start_index; i < NUM_ENTRIES; i++) {
      if (count == 0) {
        break;
      }

      table[i] = curpage->address | pe_flags;
      count--;
      virt_addr += pg_flags_to_size(curpage->flags);
      curpage = curpage->next;
    }
  }

  *TEMP_PDPTE = 0;
  cpu_invlpg(TEMP_PTR);
  critical_exit();

  if (out_pages != NULL) {
    *out_pages = LIST_FIRST(&table_pages);
  }
}


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
      table_virt[i] = (uint64_t) new_pml4->address | PE_WRITE | PE_PRESENT;
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

uintptr_t fork_page_tables(page_t **out_pages, bool deepcopy_user) {
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
      table_virt[i] = (uint64_t) new_pml4->address | PE_WRITE | PE_PRESENT;
    } else {
      table_virt[i] = PML4_PTR[i];
    }
  }

  // deep copy user entries if requested
  if (deepcopy_user) {
    for (int i = PML4_INDEX(USER_SPACE_START); i <= PML4_INDEX(USER_SPACE_END); i++) {
      page_t *page_ptr = NULL;
      uint64_t *src_table = (void *) get_virt_addr(R_ENTRY, R_ENTRY, R_ENTRY, i);
      uint64_t *dest_table = TEMP_PTR;
      TEMP_PTR[i] = recursive_duplicate_pgtable(PG_LEVEL_PDP, src_table, dest_table, i, &page_ptr);
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
