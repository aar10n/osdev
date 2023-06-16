//
// Created by Aaron Gill-Braun on 2022-06-17.
//

#include <mm/pmalloc.h>
#include <mm/vmalloc.h>
#include <mm/init.h>

#include <string.h>
#include <printf.h>
#include <panic.h>
#include <bitmap.h>
#include <init.h>

static LIST_HEAD(mem_zone_t) mem_zones[MAX_ZONE_TYPE];
static size_t zone_page_count[MAX_ZONE_TYPE];
static size_t reserved_pages = 128;

static size_t zone_limits[MAX_ZONE_TYPE] = {
  ZONE_LOW_MAX,
  ZONE_DMA_MAX,
  ZONE_NORMAL_MAX,
  ZONE_HIGH_MAX
};

const char *zone_names[MAX_ZONE_TYPE] = {
  "Low",
  "DMA",
  "Normal",
  "High"
};

#define ZONE_ALLOC_DEFAULT ZONE_TYPE_HIGH
// this specifies the zone preference order for allocating pages after the first
static mem_zone_type_t zone_alloc_order[MAX_ZONE_TYPE] = {
  [ZONE_TYPE_HIGH] = ZONE_TYPE_NORMAL,
  [ZONE_TYPE_NORMAL] = ZONE_TYPE_DMA,
  [ZONE_TYPE_DMA] = ZONE_TYPE_LOW,
  [ZONE_TYPE_LOW] = MAX_ZONE_TYPE, // out of zones
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
    if (addr >= zone->base && addr < zone->base + zone->size) {
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
    page_t *page = kmallocz(sizeof(page_t));
    page->address = frame;
    page->flags = flags;
    page->zone = zone;
    frame += stride;

    count--;
    SLIST_ADD(&pages, page, next);
  }

  return LIST_FIRST(&pages);
}

bitmap_t *alloc_pages_bitmap(size_t num_pages) {
  bitmap_t *frames = NULL;
  size_t nbytes = align(num_pages, 8) / 8;
  if (nbytes >= PAGES_TO_SIZE(2)) {
    // too large for kmalloc
    size_t num_bmp_pages = SIZE_TO_PAGES(nbytes);
    if (num_bmp_pages > reserved_pages) {
      panic("no more reserved pages (%d)", num_bmp_pages);
    }

    uintptr_t buffer_phys = mm_early_alloc_pages(num_bmp_pages);
    void *buffer = mm_early_map_pages_reserved(buffer_phys, num_bmp_pages, PG_WRITE | PG_NOCACHE);
    memset(buffer, 0, nbytes);

    frames = kmalloc(sizeof(bitmap_t));
    frames->map = buffer;
    frames->free = num_pages;
    frames->used = 0;
    frames->size = nbytes;

    reserved_pages -= num_bmp_pages;
  } else {
    frames = create_bitmap(num_pages);
  }

  return frames;
}

//

static void remap_initrd_image(void *_arg) {
  kassert(is_aligned(boot_info_v2->initrd_addr, PAGE_SIZE));
  kassert(is_aligned(boot_info_v2->initrd_size, PAGE_SIZE));

  vm_mapping_t *vm = vm_alloc_phys(boot_info_v2->initrd_addr, 0, boot_info_v2->initrd_size, 0, "initrd");
  boot_info_v2->initrd_addr = (uintptr_t) vm_map(vm, 0);
}

void init_mem_zones() {
  // reserve pages in zone entry
  mm_early_reserve_pages(reserved_pages);

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
    mem_zone_type_t end_type = get_mem_zone_type(base + size - 1);
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
      zone->frames = alloc_pages_bitmap(page_count);
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
    zone->frames = alloc_pages_bitmap(page_count);
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

  if (boot_info_v2->initrd_addr != 0) {
    size_t num_pages = SIZE_TO_PAGES(boot_info_v2->initrd_size);
    if (_reserve_pages(boot_info_v2->initrd_addr, num_pages) < 0) {
      panic("failed to reserve pages for initrd");
    }
    register_init_address_space_callback(remap_initrd_image, NULL);
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

  mem_zone_type_t zone_type = ZONE_ALLOC_DEFAULT;
  page_t *pages = NULL;
  while (pages == NULL) {
    if (zone_type == MAX_ZONE_TYPE) {
      panic("out of memory");
    }

    pages = _alloc_pages_zone(zone_type, count, flags);
    if (pages == NULL) {
      zone_type = zone_alloc_order[zone_type];
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

  if (zone == NULL && end_zone == NULL) {
    if (!(flags & PG_FORCE)) {
      panic("_alloc_pages_at: invalid address %p", address);
    }

    // construct a list of page structs
    uint64_t frame = address;
    return make_page_structs(zone, frame, count, stride, flags);
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
  if (before_count == 0 && !(flags & PG_FORCE)) {
    panic("requested pages are already allocated");
  }

  // construct a list of page structs
  uint64_t frame = zone->base + PAGES_TO_SIZE(frame_index);
  return make_page_structs(zone, frame, count, stride, flags);
}

int _reserve_pages(uintptr_t address, size_t count) {
  kassert(is_aligned(address, PAGE_SIZE));
  kassert(count > 0);

  mem_zone_type_t type = get_mem_zone_type(address);
  mem_zone_type_t end_type = get_mem_zone_type(address + PAGES_TO_SIZE(count));
  if (type != end_type) {
    panic("requested pages cross zone boundary");
  }

  // find zone which contains the requested address
  mem_zone_t *zone = locate_owning_mem_zone(address);
  mem_zone_t *end_zone = locate_owning_mem_zone(address + PAGES_TO_SIZE(count) - 1);
  if (zone != end_zone) {
    panic("requested pages cross zone boundary");
  }

  if (zone == NULL && end_zone == NULL) {
    return 0;
  }

  // mark frames as used
  kassert(zone != NULL);
  spin_lock(&zone->lock);
  size_t before_count;
  index_t frame_index = SIZE_TO_PAGES(address - zone->base);
  if (count == 1) {
    // common case - fastest
    before_count = bitmap_set(zone->frames, frame_index) ? 1 : 0;
  } else {
    // less common case - slower
    before_count = bitmap_get_n(zone->frames, frame_index, count);
    if (before_count == 0) {
      bitmap_set_n(zone->frames, frame_index, count);
    }
  }
  spin_unlock(&zone->lock);
  if (before_count == 0) {
    panic("requested pages are already allocated");
  }

  return 0;
}

/**
 * Frees one or more physical pages.
 */
void _free_pages(page_t *page) {
  while (page != NULL) {
    mem_zone_t *zone = page->zone;
    if (zone == NULL) {
      page_t *next = page->next;
      kfree(page);
      page = next;
      continue;
    }

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

//

bool mm_is_kernel_code_ptr(uintptr_t ptr) {
  return ptr >= kernel_code_start && ptr < kernel_code_end;
}

bool mm_is_kernel_data_ptr(uintptr_t ptr) {
  return ptr >= kernel_code_end && ptr < kernel_data_end;
}
