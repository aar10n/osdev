//
// Created by Aaron Gill-Braun on 2020-09-30.
//

#include <base.h>
#include <panic.h>
#include <stdio.h>

#include <mm/mm.h>
#include <mm/heap.h>
#include <string.h>

static memory_zone_t *zones[ZONE_MAX];
static bool did_initialize = false;

static zone_type_t get_zone_type(uintptr_t phys_addr) {
  if (phys_addr < Z_LOW_MAX) {
    return ZONE_LOW;
  } else if (phys_addr < Z_DMA_MAX) {
    return ZONE_DMA;
  } else if (phys_addr < Z_NORMAL_MAX) {
    return ZONE_NORMAL;
  }
  return ZONE_HIGH;
}

static zone_type_t get_next_zone(zone_type_t zone) {
  switch (zone) {
    case ZONE_LOW:
      return ZONE_NORMAL;
    case ZONE_NORMAL:
      return ZONE_HIGH;
    case ZONE_HIGH:
      return ZONE_DMA;
    default:
      // out of memory
      return ZONE_MAX;
  }
}

static uintptr_t get_zone_limit(zone_type_t zone) {
  switch (zone) {
    case ZONE_LOW:
      return Z_LOW_MAX;
    case ZONE_DMA:
      return Z_DMA_MAX;
    case ZONE_NORMAL:
      return Z_NORMAL_MAX;
    default:
      return UINT64_MAX;
  }
}

static const char *get_zone_name(zone_type_t zone) {
  switch (zone) {
    case ZONE_LOW:
      return "ZONE_LOW";
    case ZONE_DMA:
      return "ZONE_DMA";
    case ZONE_NORMAL:
      return "ZONE_NORMAL";
    case ZONE_HIGH:
      return "ZONE_HIGH";
    default:
      return "ZONE_UNKNOWN";
  }
}

static bool does_cross_zone(memory_region_t *region) {
  zone_type_t start = get_zone_type(region->phys_addr);
  zone_type_t end = get_zone_type(region->phys_addr + region->size);
  return start != end;
}

static void apply_page_flags(page_t *page, uint16_t flags) {
  kassert(!(flags & PE_SIZE));
  bool is_alt_size = (flags & PE_2MB_SIZE) || (flags & PE_1GB_SIZE);

  page->flags.present = 0;
  page->flags.write = (flags & PE_WRITE) != 0;
  page->flags.user = (flags & PE_USER) != 0;
  page->flags.write_through = (flags & PE_WRITE_THROUGH) != 0;
  page->flags.cache_disable = (flags & PE_CACHE_DISABLE) != 0;
  page->flags.page_size = is_alt_size;
  page->flags.global = (flags & PE_GLOBAL) != 0;
  page->flags.page_size_2mb = (flags & PE_2MB_SIZE) != 0;
  page->flags.page_size_1gb = (flags & PE_1GB_SIZE) != 0;
  page->flags.reserved = 0;
}

//

void mm_init() {
  kprintf("[mm] initializing\n");
  memory_map_t *mem = boot_info->mem_map;

  memory_zone_t *last = NULL;
  memory_region_t *region = mem->mmap;
  while ((uintptr_t) region < (uintptr_t) mem->mmap + mem->mmap_size) {
    if (region->type != MEMORY_FREE) {
      region++;
      continue;
    }

    zone_type_t zone_type = get_zone_type(region->phys_addr);
    if (does_cross_zone(region)) {
      kprintf("[mm] splitting region at zone limit\n");

      // split the current region at the zone limit
      uintptr_t limit = get_zone_limit(zone_type);

      // split the memory map
      kassert(mem->mmap_size < mem->mmap_capacity);
      uintptr_t mmap_end = (uintptr_t) mem->mmap + mem->mmap_size;
      memmove(region + 1, region, mmap_end - (uintptr_t) region);

      size_t old_size = region->size;
      region->size = limit - region->phys_addr - 1;
      (region + 1)->phys_addr = limit;
      (region + 1)->size = old_size - region->size;

      continue;
    }

    memory_zone_t *zone = kmalloc(sizeof(memory_zone_t));
    zone->type = zone_type;
    zone->base_addr = region->phys_addr;
    zone->size = region->size;

    size_t size = max((region->size / PAGE_SIZE) / 64, 1) * sizeof(uint64_t);
    // kprintf("size: %d\n", size);
    // kprintf("region->size: %u\n", region->size);
    uint64_t *map = kmalloc(size);

    bitmap_t *bitmap = kmalloc(sizeof(bitmap_t));
    bitmap->size = size;
    bitmap->free = size * 8;
    bitmap->used = 0;
    bitmap->map = map;

    zone->pages = bitmap;

    // kprintf("%s\n", get_zone_name(zone->type));
    // kprintf("  base_addr: %p\n", zone->base_addr);
    // kprintf("  size: %d\n", zone->size);
    // kprintf("  pages:\n");
    // kprintf("    size: %u\n", bitmap->size);
    // kprintf("    free: %u\n", bitmap->free);
    // kprintf("    used: %u\n", bitmap->used);
    // kprintf("    map: %p\n", bitmap->map);

    if (last == NULL || last->type != zone->type) {
      zones[zone->type] = zone;
      last = zone;
    } else if (last->type) {
      last->next = zone;
    }

    region++;
  }

  did_initialize = true;
  kprintf("[mm] done!\n");
}

page_t *mm_alloc_page(zone_type_t zone_type, uint16_t flags) {
  kassert(did_initialize);
  kassert(zone_type < ZONE_MAX);

  memory_zone_t *zone = zones[zone_type];
  while (!zone || zone->pages->free == 0) {
    if (!zone || !zone->next) {
      if (flags & PE_ASSERT_ZONE) {
        panic("panic - failed to allocate page in %s\n", get_zone_name(zone_type));
      }

      kprintf("[memory] trying another zone\n");

      zone_type_t next_zone = get_next_zone(zone ? zone->type : zone_type);
      if (next_zone == ZONE_MAX) {
        panic("panic - out of memory\n");
      }

      zone = zones[next_zone];
    } else {
      zone = zone->next;
    }
  }

  index_t frame_index = bitmap_get_free(zone->pages);
  bitmap_set(zone->pages, frame_index);
  uint64_t frame = zone->base_addr + PAGES_TO_SIZE(frame_index);

  page_t *page = kmalloc(sizeof(page_t));
  page->frame = frame;
  page->entry = NULL;
  page->next = NULL;

  apply_page_flags(page, flags);
  page->flags.zone = zone->type;

  return page;
}

void mm_free_page(page_t *page) {
  while (page) {
    memory_zone_t *zone = zones[page->flags.zone];
    index_t index = SIZE_TO_PAGES(page->frame - zone->base_addr);
    bitmap_clear(zone->pages, index);

    page_t *next = page->next;
    kfree(page);
    page = next;
  }
}
