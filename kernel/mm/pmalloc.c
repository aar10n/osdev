//
// Created by Aaron Gill-Braun on 2022-06-17.
//

#include <mm/pmalloc.h>
#include <mm/init.h>

#include <string.h>
#include <printf.h>
#include <panic.h>
#include <bitmap.h>

void *kmalloc(size_t size) __malloc_like;
void kfree(void *ptr);

LIST_HEAD(mem_zone_t) mem_zones[MAX_ZONE_TYPE];
size_t zone_page_count[MAX_ZONE_TYPE];

size_t zone_limits[MAX_ZONE_TYPE] = {
  ZONE_LOW_MAX,
  ZONE_DMA_MAX,
  ZONE_NORMAL_MAX,
  ZONE_HIGH_MAX
};

mem_zone_type_t next_zone_type[MAX_ZONE_TYPE] = {
  ZONE_TYPE_NORMAL,
  ZONE_TYPE_HIGH,
  ZONE_TYPE_DMA,
  MAX_ZONE_TYPE
};

const char *zone_names[MAX_ZONE_TYPE] = {
  "Low",
  "DMA",
  "Normal",
  "High"
};

mem_zone_type_t get_mem_zone_type(uintptr_t addr) {
  if (addr < ZONE_LOW_MAX) {
    return ZONE_TYPE_LOW;
  } else if (addr < ZONE_DMA_MAX) {
    return ZONE_TYPE_DMA;
  } else if (addr < ZONE_NORMAL_MAX) {
    return ZONE_TYPE_NORMAL;
  }
  return ZONE_TYPE_HIGH;
}

mem_zone_t *locate_owning_mem_zone(uintptr_t addr) {
  mem_zone_type_t zone_type = get_mem_zone_type(addr);
  mem_zone_t *zone = LIST_FIRST(&mem_zones[zone_type]);
  while (zone != NULL) {
    if (zone->base >= addr && addr < zone->base + zone->size) {
      return zone;
    }
    zone = LIST_NEXT(zone, list);
  }
  return NULL;
}

page_t *make_page_structs(mem_zone_t *zone, uint64_t frame, size_t count, size_t stride, uint32_t flags) {
  size_t total_size = count * stride;
  kassert(total_size < UINT32_MAX);

  LIST_HEAD(page_t) pages = LIST_HEAD_INITR;
  while (count > 0) {
    page_t *page = kmalloc(sizeof(page_t));
    page->address = frame;
    page->flags = flags;
    page->reserved.raw = 0;
    page->mapping = NULL;
    page->zone = zone;
    frame += stride;

    count--;
    SLIST_ADD(&pages, page, next);
  }

  LIST_LAST(&pages)->flags |= PG_LIST_TAIL;
  page_t *first = LIST_FIRST(&pages);
  first->flags |= PG_LIST_HEAD;
  first->head.list_sz = (uint32_t) total_size;
  return first;
}

//

void init_mem_zones() {
  memory_map_t *memory_map = &boot_info_v2->mem_map;
  size_t num_entries = memory_map->size / sizeof(memory_map_entry_t);
  for (size_t i = 0; i < num_entries; i++) {
    memory_map_entry_t *entry = &memory_map->map[i];
    if (entry->type != MEMORY_USABLE) {
      continue;
    }

    uintptr_t base = entry->base;
    size_t size = entry->size;

    mem_zone_type_t type = get_mem_zone_type(base);
    mem_zone_type_t end_type = get_mem_zone_type(base + size);
    if (type != end_type) {
      // an entry should never cross more than two zones
      kassert(end_type - type == 1);

      // create new zone for end of entry
      mem_zone_t *zone = kmalloc(sizeof(mem_zone_t));
      zone->type = end_type;
      zone->base = zone_limits[end_type];
      zone->size = base + size - zone->base;

      spin_init(&zone->lock);

      size_t page_count = SIZE_TO_PAGES(zone->size);
      zone->frames = create_bitmap(page_count);
      LIST_ADD(&mem_zones[end_type], zone, list);
      zone_page_count[end_type] += page_count;

      // shrink original entry to end of zone
      size = zone->base - base;
    }

    mem_zone_t *zone = kmalloc(sizeof(mem_zone_t));
    zone->type = type;
    zone->base = base;
    zone->size = size;

    spin_init(&zone->lock);

    size_t page_count = SIZE_TO_PAGES(size);
    zone->frames = create_bitmap(page_count);
    LIST_ADD(&mem_zones[type], zone, list);
    zone_page_count[type] += page_count;
  }

  kprintf("memory zones:\n");
  for (size_t i = 0; i < MAX_ZONE_TYPE; i++) {
    uintptr_t zone_start = i == 0 ? 0 : zone_limits[i - 1];
    uintptr_t zone_end = zone_limits[i];

    size_t pad_len = 6 - strlen(zone_names[i]);
    char pad[6];
    memset(pad, ' ', pad_len);
    pad[pad_len] = '\0';

    kprintf("  %s zone:%s [%018p-%018p] %d pages\n", zone_names[i], pad, zone_start, zone_end, zone_page_count[i]);
  }
}

//

/**
 * Allocates one or more physical pages from the given zone. If there is insufficient
 * memory available to satisfy the request the function will return NULL, unless the
 * PG_FORCE flag is set in which case the function will panic instead.
 */
page_t *_alloc_pages_zone(mem_zone_type_t zone_type, size_t count, uint32_t flags) {
  kassert(zone_type < MAX_ZONE_TYPE);
  if (count == 0) {
    return NULL;
  }

  size_t num_4k_pages = count;
  size_t align = 0;
  size_t stride = PAGE_SIZE;
  if (flags & PG_BIGPAGE) {
    kassert((flags & PG_HUGEPAGE) == 0);
    num_4k_pages = SIZE_TO_PAGES(count * PAGE_SIZE_2MB);
    align = SIZE_TO_PAGES(PAGE_SIZE_2MB);
    stride = PAGE_SIZE_2MB;
  } else if (flags & PG_HUGEPAGE) {
    num_4k_pages = SIZE_TO_PAGES(count * PAGE_SIZE_1GB);
    align = SIZE_TO_PAGES(PAGE_SIZE_1GB);
    stride = PAGE_SIZE_1GB;
  }

  // find zone to accommodate allocation
  mem_zone_t *zone = LIST_FIRST(&mem_zones[zone_type]);
  while (!zone || zone->frames->free < num_4k_pages) {
    zone = zone ? LIST_NEXT(zone, list) : NULL;
    if (!zone) {
      if (flags & PG_FORCE) {
        panic("out of memory in zone %s", zone_names[zone_type]);
      }
      return NULL;
    }
  }

  // mark frames as used
  spin_lock(&zone->lock);
  index_t frame_index;
  if (num_4k_pages == 1) {
    // common case - fastest
    frame_index = bitmap_get_set_free(zone->frames);
  } else {
    // less common case - slower
    frame_index = bitmap_get_set_nfree(zone->frames, num_4k_pages, align);
  }
  spin_unlock(&zone->lock);
  if (frame_index < 0) {
    panic("out of memory");
  }

  // construct a list of page structs
  uint64_t frame = zone->base + PAGES_TO_SIZE(frame_index);
  return make_page_structs(zone, frame, count, stride, flags);
}

/**
 * Allocates one or more physical pages from any zone. If there is insufficient
 * memory available to satisfy the request the function panic.
 */
page_t *_alloc_pages(size_t count, uint32_t flags) {
  flags &= ~PG_FORCE;

  mem_zone_type_t zone_type = ZONE_TYPE_NORMAL;
  page_t *pages = NULL;
  while (pages == NULL) {
    if (zone_type == MAX_ZONE_TYPE) {
      panic("out of memory");
    }

    pages = _alloc_pages_zone(zone_type, count, flags);
    if (pages == NULL) {
      zone_type = next_zone_type[zone_type];
    }
  }

  return pages;
}

/**
 * Allocates one or more physical pages starting at the given physical address.
 * If there is insufficient memory available to satisfy the request the function
 * will return NULL, unless the PG_FORCE flag is set in which case it will panic.
 */
page_t *_alloc_pages_at(uintptr_t address, size_t count, uint32_t flags) {
  kassert(is_aligned(address, PAGE_SIZE));
  if (count == 0) {
    return NULL;
  }

  size_t num_4k_pages = count;
  size_t stride = PAGE_SIZE;
  if (flags & PG_BIGPAGE) {
    kassert((flags & PG_HUGEPAGE) == 0);
    kassert(is_aligned(address, PAGE_SIZE_2MB));
    num_4k_pages = SIZE_TO_PAGES(count * PAGE_SIZE_2MB);
    stride = PAGE_SIZE_2MB;
  } else if (flags & PG_HUGEPAGE) {
    kassert(is_aligned(address, PAGE_SIZE_1GB));
    num_4k_pages = SIZE_TO_PAGES(count * PAGE_SIZE_1GB);
    stride = PAGE_SIZE_1GB;
  }

  mem_zone_type_t type = get_mem_zone_type(address);
  mem_zone_type_t end_type = get_mem_zone_type(address + PAGES_TO_SIZE(num_4k_pages));
  if (type != end_type) {
    panic("requested pages cross zone boundary");
  }

  // find zone which contains the requested address
  mem_zone_t *zone = locate_owning_mem_zone(address);
  mem_zone_t *end_zone = locate_owning_mem_zone(address + PAGES_TO_SIZE(num_4k_pages) - 1);
  if (zone != end_zone) {
    panic("requested pages cross zone boundary");
  }

  // mark frames as used
  kassert(zone != NULL);
  spin_lock(&zone->lock);
  size_t before_count;
  index_t frame_index = SIZE_TO_PAGES(address - zone->base);
  if (num_4k_pages == 1) {
    // common case - fastest
    before_count = bitmap_set(zone->frames, frame_index) ? 1 : 0;
  } else {
    // less common case - slower
    before_count = bitmap_get_n(zone->frames, frame_index, num_4k_pages);
    if (before_count == 0) {
      bitmap_set_n(zone->frames, frame_index, num_4k_pages);
    }
  }
  spin_unlock(&zone->lock);
  if (before_count == 0) {
    panic("requested pages are already allocated");
  }

  // construct a list of page structs
  uint64_t frame = zone->base + PAGES_TO_SIZE(frame_index);
  return make_page_structs(zone, frame, count, stride, flags);
}

/**
 * Frees one or more physical pages.
 */
void _free_pages(page_t *page) {
  while (page != NULL) {
    mem_zone_t *zone = page->zone;
    kassert(zone);

    size_t num_4k_pages = 1;
    if (page->flags & PG_BIGPAGE) {
      num_4k_pages = SIZE_TO_PAGES(PAGE_SIZE_2MB);
    } else if (page->flags & PG_HUGEPAGE) {
      num_4k_pages = SIZE_TO_PAGES(PAGE_SIZE_1GB);
    }

    spin_lock(&zone->lock);
    index_t index = SIZE_TO_PAGES(page->address - zone->base);
    if (num_4k_pages == 1) {
      bitmap_clear(zone->frames, index);
    } else {
      bitmap_clear_n(zone->frames, index, num_4k_pages);
    }
    spin_unlock(&zone->lock);

    page_t *next = page->next;
    kfree(page);
    page = next;
  }
}
