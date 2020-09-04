//
// Created by Aaron Gill-Braun on 2019-04-29.
//

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <kernel/mem/heap.h>
#include <kernel/mem/mm.h>

typedef struct free_pages {
  int count;
  page_t *first;
} free_pages_t;

typedef struct page_zone {
  const char *name;
  size_t max_length;
  free_pages_t free_pages[MAX_ORDER + 1];
} page_zone_t;

page_zone_t zones[3] = {
    { .name = "ZONE_DMA", .max_length = 0x1000000 },     // 16 MiB
    { .name = "ZONE_NORMAL", .max_length = 0x32000000 }, // 800 MiB
    { .name = "ZONE_HIGHMEM", .max_length = 0xFFFFFFFF } // 4 GiB
};

// free_pages_t free[MAX_ORDER + 1];

static inline free_pages_t *get_free_pages(page_t *page) {
  return zones[page->zone].free_pages;
}

static inline uint8_t get_zone(uint8_t flags) {
  uint8_t zone = (flags & 0x3);
  switch (zone) {
    case ZONE_DMA:
      return 0;
    case ZONE_NORMAL:
      return 1;
    case ZONE_HIGHMEM:
      return 2;
    default:
      return 1;
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
    kprintf("error: no free pages");
    return NULL;
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

    int chunk_log = log2(chunk_size / PAGE_SIZE);
    int order = chunk_log > MAX_ORDER ? MAX_ORDER : chunk_log;
    int block_size = (1 << order) * PAGE_SIZE;
    int block_count = chunk_size / block_size;

    int pages_created = 0;
    page_t *prev_page = NULL;
    for (int i = 0; i < block_count; i++) {
      page_t *page = kmalloc(sizeof(page_t));
      page->phys_addr = cur_addr;
      page->virt_addr = (uintptr_t) NULL;
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
  size_t offset = 0;
  for (int i = 0; i < 3; i++) {
    page_zone_t *zone = &zones[i];
    // kprintf("\ninitializing zone %s\n", zone->name);
    if (size <= zone->max_length) {
      mem_init_zone(base_addr + offset, size, i);
      // kprintf("zone %s initialized!\n", zones[i].name);
      break;
    } else {
      mem_init_zone(base_addr + offset, zone->max_length, i);
      offset += zone->max_length;
      size -= zone->max_length;
      // kprintf("zone %s initialized!\n", zones[i].name);
    }
  }


}

page_t *alloc_pages(int order, uint8_t flags) {
  uint8_t zone = get_zone(flags);
  free_pages_t *free = zones[zone].free_pages;
  // kprintf("allocating page of order %d (%s)\n", order, zones[zone].name);

  if (order > MAX_ORDER || order < 0) {
    // error
    kprintf("error: invalid allocation order\n");
    return NULL;
  } else if (free[order].count == 0) {
    if (order == MAX_ORDER) {
      // no free pages
      kprintf("error: no free pages");
      return NULL;
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
  int depth = get_tree_depth(page);
  int order_max = page->order;

  char buffer[4096];
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
