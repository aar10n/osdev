//
// Created by Aaron Gill-Braun on 2019-04-29.
//

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <kernel/mem/heap.h>
#include <kernel/mem/mm.h>
#include <kernel/panic.h>

#define ZONE(t, l) \
  { .name = #t, .type = t, .length = l }

#define NUM_ZONES \
  (sizeof(zones) / sizeof(zones[0]))


// Define the memory zones
mem_zone_t zones[] = {
    ZONE(ZONE_RESRV, 0x200000),    // 2 MiB - kernel
    ZONE(ZONE_DMA, 0xE00000),      // 14 MiB - available
    ZONE(ZONE_RESRV, 0x400000),    // 4 MiB - reserved
    ZONE(ZONE_NORMAL, 0x32000000), // 800 MiB - available
    ZONE(ZONE_HIGHMEM, 0xFFFFFFFF) // 4 GiB - available
};

// this is a 1024x1024 array of pointers to page structs
// and it allows you to find the page for any given virtual
// address by indexing it like the page directory/table.
page_t **mem = NULL;

// Helper functions

static inline free_pages_t *get_free_pages(page_t *page) {
  return zones[page->zone].free_pages;
}

static inline uint8_t get_zone(uint8_t flags) {
  uint8_t zone = (flags & 0x3);
  // index into zones array
  switch (zone) {
    case ZONE_DMA:
      return 1;
    case ZONE_NORMAL:
      return 3;
    case ZONE_HIGHMEM:
      return 4;
    default:
      return 3;
  }
}

static inline const char *zone_name(page_t *page) {
  return zones[page->zone].name;
}

//

void add_page(page_t *page) {
  kprintf("adding page of order %d (%s)\n", page->order, zone_name(page));
  free_pages_t *free = get_free_pages(page);

  int order = page->order;
  if (free[order].first == NULL) {
    free[order].first = page;
    free[order].count = 1;
    page->next = page;
    page->prev = page;
    return;
  }

  page->next = free[order].first;
  page->prev = free[order].first->prev;

  free[order].first->prev->next = page;
  free[order].first->prev = page;
  free[order].count += 1;
}

void remove_page(page_t *page) {
  kprintf("removing page of order %d (%s)\n", page->order, zone_name(page));
  free_pages_t *free = get_free_pages(page);

  int order = page->order;
  if (free[order].count == 1) {
    free[order].first = NULL;
    free[order].count = 0;
    return;
  }

  page->prev->next = page->next;
  page->next->prev = page->prev;

  if (free[order].first == page) {
    free[order].first = page->next;
  }
  free[order].count -= 1;
}

page_t *get_buddy(page_t *page) {
  if (!page->parent) {
    return NULL;
  }

  if (page->flags.head) {
    return page->parent->tail;
  } else if (page->flags.tail) {
    return page->parent->head;
  }
  return NULL;
}

void rejoin_page(page_t *page) {
  if (page->order == 0) {
    return;
  }

  kprintf("rejoining page of order %d (%s)\n", page->order, zone_name(page));

  page->flags.split = false;

  page_t *head = page->head;
  page_t *tail = page->tail;

  remove_page(head);
  remove_page(tail);

  // free head
  // free tail

  page_t *buddy = get_buddy(page);
  if (buddy) mm_print_debug_page(buddy);
  if (buddy && buddy->flags.free) {
    rejoin_page(page->parent);
  } else {
    add_page(page);
  }

  page->head = NULL;
  page->tail = NULL;
}

page_t *split_page(page_t *page) {
  if (page->order == 0) {
    return NULL;
  }

  kprintf("splitting page of order %d (%s)\n", page->order, zone_name(page));

  page_t *head = kmalloc(sizeof(page_t));
  page_t *tail = kmalloc(sizeof(page_t));

  head->phys_addr = page->phys_addr;
  head->flags.raw = 0;
  head->flags.free = true;
  head->flags.head = true;
  head->zone = page->zone;
  head->order = page->order -1;
  head->parent = page;

  tail->phys_addr = head->phys_addr ^ (1 << head->order);
  tail->flags.raw = 0;
  tail->flags.free = true;
  tail->flags.tail = true;
  tail->zone = page->zone;
  tail->order = page->order - 1;
  tail->parent = page;

  page->flags.free = false;
  page->flags.split = true;
  page->head = head;
  page->tail = tail;

  add_page(head);
  add_page(tail);
  remove_page(page);

  return head;
}

//
//
//

page_t *mem_split(int order, int zone) {
  kprintf("trying to split page from order %d (%s)\n", order, zones[zone].name);
  free_pages_t *free = zones[zone].free_pages;

  page_t *page;
  if (order == MAX_ORDER) {
    // no free pages
    panic("no free pages");
  } else if (free[order].count == 0) {
    kprintf("no pages available\n");
    page = mem_split(order + 1, zone);
  } else {
    page = free[order].first;
  }

  return split_page(page);
}

//

void mem_init_zone(uintptr_t base_addr, size_t size, uint8_t zone) {
  free_pages_t *free = zones[zone].free_pages;

  // kprintf("%s\n", zones[zone].name);
  // kprintf("start_addr: 0x%08x\n", base_addr);
  // kprintf("size: %d MiB (%d B)\n", size / (1024 * 1024), size);

  size_t remaining = size;
  uintptr_t cur_addr = base_addr;
  while (remaining >= PAGE_SIZE) {
    size_t next_pow = next_pow2(remaining);

    size_t chunk_size;
    if (next_pow == remaining) {
      chunk_size = remaining;
    } else {
      chunk_size = umax(PAGE_SIZE, next_pow >> 1);
    }
    // kprintf("chunk size: %d MiB (%d B)\n", chunk_size / (1024 * 1024), chunk_size);
    remaining -= chunk_size;

    uint32_t chunk_log = log2(chunk_size / PAGE_SIZE);
    uint32_t order = chunk_log > MAX_ORDER ? MAX_ORDER : chunk_log;
    size_t block_size = (1 << order) * PAGE_SIZE;
    uint32_t block_count = chunk_size / block_size;

    int pages_created = 0;
    page_t *prev_page = NULL;
    for (int i = 0; i < block_count; i++) {
      // kprintf("%s - %d | %p (%p)\n", zones[zone].name, order, cur_addr, phys_to_virt(cur_addr));

      // create the page struct
      uintptr_t virt_addr = phys_to_virt(cur_addr);

      page_t *page = kmalloc(sizeof(page_t));
      page->phys_addr = cur_addr;
      page->virt_addr = virt_addr;
      page->zone = zone;
      page->order = order;
      page->flags.raw = 0;
      page->flags.free = true;

      if (prev_page) {
        prev_page->next = page;
        page->prev = prev_page;
      } else {
        free[order].first = page;
      }

      // assign the range in the mem array this page represents
      // to the pointer for this page
      // uintptr_t upper_bound = virt_addr + block_size;
      // for (uintptr_t j = virt_addr; j < upper_bound; j += PAGE_SIZE) {
      //   int pde_index = addr_to_pde(j);
      //   int pte_index = addr_to_pte(j);
      //   kprintf("adding to mem array - mem[%d][%d]\n", pde_index, pte_index);
      //   mem[pde_index + pte_index] = page;
      // }

      prev_page = page;
      cur_addr += block_size;
      pages_created++;
    }

    // kprintf("%d pages of order %d created in zone %s\n", pages_created, order, zones[zone].name);

    // link first and last pages
    free[order].count += pages_created;
    free[order].first->prev = prev_page;
    prev_page->next = free[order].first;
  }
}

void mem_init(uintptr_t base_addr, size_t size) {
  if (size < MIN_MEMORY) {
    uint32_t mib = MIN_MEMORY / (1024 * 1024);
    panic("not enough memory - a minimum of %u is required", mib);
  }

  kprintf("initializing memory\n");

  // first clear the mem array
  // memset(mem, 0, 1024 * 1024 * sizeof(page_t *));

  size_t offset = 0;
  for (int i = 0; i < NUM_ZONES; i++) {
    mem_zone_t zone = zones[i];
    if (zone.type == ZONE_RESRV) {
      if (size <= zone.length) {
        panic("not enough memory for reserved zone %d", i);
      }

      kprintf("reserving zone\n");
      offset += zone.length;
      size -= zone.length;
      continue;
    }

    kprintf("initializing zone %s\n", zone.name);
    if (size <= zone.length) {
      mem_init_zone(base_addr + offset, size, i);
      // kprintf("zone %s initialized!\n", zones[i].name);
      break;
    } else {
      mem_init_zone(base_addr + offset, zone.length, i);
      offset += zone.length;
      size -= zone.length;
      // kprintf("zone %s initialized!\n", zones[i].name);
    }
  }


}

page_t *alloc_pages(int order, uint8_t flags) {
  uint8_t zone = get_zone(flags);
  free_pages_t *free = zones[zone].free_pages;
  // kprintf("allocating page of order %d (%s)\n", order, zones[zone].name);

  kassert(order <= MAX_ORDER && order > 0);
  if (free[order].count == 0) {
    if (order == MAX_ORDER) {
      // no free pages
      panic("no free pages");
    }

    page_t *page = mem_split(order + 1, zone);
    page->flags.free = false;
    remove_page(page);
    return page;
  }

  page_t *page = free[order].first;
  page->flags.free = false;
  remove_page(page);
  return page;
}

page_t *alloc_page(uint8_t flags) {
  return alloc_pages(0, flags);
}

void free_page(page_t *page) {
  kprintf("freeing page of order %d (%s)\n", page->order, zone_name(page));
  page->flags.free = true;
  add_page(page);

  page_t *buddy = get_buddy(page);
  if (buddy && buddy->flags.free) {
    rejoin_page(page->parent);
  }
}

// Debugging

int get_tree_depth(page_t *page) {
  if (page->flags.split) {
    int left = get_tree_depth(page->head);
    int right = get_tree_depth(page->head);
    return imax(left, right) + 1;
  }
  return 1;
}

int print_node(char *buffer, page_t *page, char *prefix, char *child_prefix) {
  int n = 0;

  int prefix_len = strlen(prefix);
  memcpy(buffer, prefix, prefix_len);
  n += prefix_len;
  n += ksprintf(buffer + n, "%d\n", page->order) - 1;

  if (page->flags.split) {
    int child_prefix_len = strlen(child_prefix);

    char *new_prefix1 = kmalloc(child_prefix_len + 5);
    memcpy(new_prefix1, child_prefix, child_prefix_len);
    memcpy(new_prefix1 + child_prefix_len, "+-- ", 5);

    char *new_prefix2 = kmalloc(child_prefix_len + 5);
    memcpy(new_prefix2, child_prefix, child_prefix_len);
    memcpy(new_prefix2 + child_prefix_len, "|   ", 5);

    char *new_prefix3 = kmalloc(child_prefix_len + 5);
    memcpy(new_prefix3, child_prefix, child_prefix_len);
    memcpy(new_prefix3 + child_prefix_len, "    ", 5);

    n += print_node(buffer + n, page->head, new_prefix1, new_prefix2);
    n += print_node(buffer + n, page->tail, new_prefix1, new_prefix3);
  }

  return n;
}

void print_buddy_tree(page_t *page) {
  static char buffer[4096];
  memset(buffer, 0, 512);
  print_node(buffer, page, "", "");
  kprintf(buffer);
}

void mm_print_debug_stats() {}

void mm_print_debug_page(page_t *page) {

  if (page->flags.split) {
    kprintf("page = { \n"
            "  virt_addr = %p\n"
            "  phys_addr = %p\n"
            "  flags = %#b\n"
            "  zone = %s\n"
            "  order = %d\n"
            "  parent = %p\n"
            "  head = %p\n"
            "  tail = %p\n"
            "}\n",
            page->virt_addr,
            page->phys_addr,
            page->flags,
            zone_name(page),
            page->order,
            page->parent,
            page->head,
            page->tail);
  } else {
    kprintf("\npage = { \n"
            "  virt_addr = %p\n"
            "  phys_addr = %p\n"
            "  flags = %#b\n"
            "  zone = %s\n"
            "  order = %d\n"
            "  parent = %p\n"
            "  next = %p\n"
            "  prev = %p\n"
            "}\n",
            page->virt_addr,
            page->phys_addr,
            page->flags,
            zone_name(page),
            page->order,
            page->parent,
            page->next,
            page->prev);
  }

  kprintf("flags = {\n"
          "  free = %d\n"
          "  split = %d\n"
          "  head = %d\n"
          "  tail = %d\n"
          "  present = %d\n"
          "  readwrite = %d\n"
          "  user = %d\n"
          "}\n",
          page->flags.free,
          page->flags.split,
          page->flags.head,
          page->flags.tail,
          page->flags.present,
          page->flags.readwrite,
          page->flags.user);

  if (page->flags.split) {
    print_buddy_tree(page);
  }
}
