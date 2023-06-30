//
// Created by Aaron Gill-Braun on 2022-06-17.
//

#include <kernel/mm/pmalloc.h>
#include <kernel/mm/vmalloc.h>
#include <kernel/mm/pgtable.h>
#include <kernel/mm/init.h>

#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/panic.h>
#include <bitmap.h>

#define ASSERT(x) kassert(x)

#define ZONE_ALLOC_DEFAULT ZONE_TYPE_HIGH

static LIST_HEAD(frame_allocator_t) mem_zones[MAX_ZONE_TYPE];
static size_t zone_page_count[MAX_ZONE_TYPE];
static size_t reserved_pages = 128;

static const size_t zone_limits[MAX_ZONE_TYPE] = {
  ZONE_LOW_MAX, ZONE_DMA_MAX, ZONE_NORMAL_MAX, ZONE_HIGH_MAX
};
static const char *zone_names[MAX_ZONE_TYPE] = {
  "Low", "DMA", "Normal", "High"
};

// this specifies the zone preference order for allocating pages
static zone_type_t zone_alloc_order[MAX_ZONE_TYPE] = {
  [ZONE_TYPE_HIGH]    = ZONE_TYPE_NORMAL,
  [ZONE_TYPE_NORMAL]  = ZONE_TYPE_DMA,
  [ZONE_TYPE_DMA]     = ZONE_TYPE_LOW,
  [ZONE_TYPE_LOW]     = MAX_ZONE_TYPE, // out of zones
};

zone_type_t get_mem_zone_type(uintptr_t addr) {
  if (addr < ZONE_LOW_MAX) {
    return ZONE_TYPE_LOW;
  } else if (addr < ZONE_DMA_MAX) {
    return ZONE_TYPE_DMA;
  } else if (addr < ZONE_NORMAL_MAX) {
    return ZONE_TYPE_NORMAL;
  }
  return ZONE_TYPE_HIGH;
}

frame_allocator_t *locate_owning_allocator(uintptr_t addr) {
  zone_type_t zone_type = get_mem_zone_type(addr);
  struct frame_allocator *fa = LIST_FIRST(&mem_zones[zone_type]);
  while (fa != NULL) {
    if (addr >= fa->base && addr < fa->base + fa->size) {
      return fa;
    }
    fa = LIST_NEXT(fa, list);
  }
  return NULL;
}

static page_t *alloc_page_structs(frame_allocator_t *fa, uintptr_t frame, size_t count, size_t pagesize, uint32_t pg_flags) {
  if (count == 0) {
    return NULL;
  }

  // mask out all but the internal flags
  pg_flags &= ~(PG_FLAGS_MASK|PG_BIGPAGE|PG_HUGEPAGE);
  if (pagesize == BIGPAGE_SIZE) {
    pg_flags |= PG_BIGPAGE;
  } else if (pagesize == HUGEPAGE_SIZE) {
    pg_flags |= PG_HUGEPAGE;
  }

  uintptr_t address = frame;
  size_t remaining = count;
  LIST_HEAD(page_t) pages = {0};
  while (remaining > 0) {
    page_t *page = kmallocz(sizeof(page_t));
    page->address = address;
    page->flags = pg_flags;
    page->fa = fa;
    SLIST_ADD(&pages, page, next);

    address += pagesize;
    remaining--;
  }

  page_t *head = LIST_FIRST(&pages);
  head->flags |= PG_HEAD;
  head->head.count = count;
  return head;
}

//
// MARK: bitmap frame allocator
//

static void *bitmap_fa_init(frame_allocator_t *fa) {
  size_t num_frames = align(fa->size, PAGE_SIZE) / PAGE_SIZE;

  bitmap_t *frames = NULL;
  size_t nbytes = align(num_frames, 8) / 8;
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
    frames->free = num_frames;
    frames->used = 0;
    frames->size = nbytes;

    reserved_pages -= num_bmp_pages;
  } else {
    frames = create_bitmap(num_frames);
  }

  return frames;
}

static intptr_t bitmap_fa_alloc(frame_allocator_t *fa, size_t count, size_t pagesize) {
  bitmap_t *frames = fa->data;
  if (frames->free == 0) {
    return -1;
  }

  size_t num_4k_pages = num_4k_pages = (count * pagesize) / PAGE_SIZE;
  index_t frame_index;
  SPIN_LOCK(&fa->lock);
  if (num_4k_pages == 1) {
    // common case - fastest
    frame_index = bitmap_get_set_free(frames);
  } else {
    // less common case - slower
    size_t align = pagesize == PAGE_SIZE ? 0 : SIZE_TO_PAGES(pagesize);
    frame_index = bitmap_get_set_nfree(frames, num_4k_pages, align);
  }

  if (frame_index >= 0) {
    fa->free -= count * pagesize;
  }
  SPIN_UNLOCK(&fa->lock);
  if (frame_index < 0) {
    return -1;
  }

  return (intptr_t) fa->base + PAGES_TO_SIZE(frame_index);
}

static int bitmap_fa_reserve(frame_allocator_t *fa, uintptr_t frame, size_t count, size_t pagesize) {
  bitmap_t *frames = fa->data;
  if (frames->free == 0) {
    return -1;
  }

  size_t num_4k_pages = num_4k_pages = (count * pagesize) / PAGE_SIZE;
  size_t before_count;
  index_t frame_index = SIZE_TO_PAGES(frame - fa->base);
  // mark frames as used
  SPIN_LOCK(&fa->lock);
  if (num_4k_pages == 1) {
    // common case - fastest
    before_count = bitmap_set(frames, frame_index) ? 1 : 0;
  } else {
    // less common case - slower
    before_count = bitmap_get_n(frames, frame_index, num_4k_pages);
    if (before_count == 0) {
      // all of the bits are zero so we can mark the range as taken
      bitmap_set_n(frames, frame_index, num_4k_pages);
    }
  }

  if (before_count == 0) {
    fa->free -= count * pagesize;
  }
  SPIN_UNLOCK(&fa->lock);
  if (before_count != 0) {
    // some or all of the requested pages are allocated
    return -1;
  }

  return 0;
}

static void bitmap_fa_free(frame_allocator_t *fa, uintptr_t frame, size_t count, size_t pagesize) {
  bitmap_t *frames = fa->data;
  size_t num_4k_pages = num_4k_pages = (count * pagesize) / PAGE_SIZE;

  index_t frame_index = SIZE_TO_PAGES(frame - fa->base);
  SPIN_LOCK(&fa->lock);
  if (num_4k_pages == 1) {
    // common case - fastest
    bitmap_clear(frames, frame_index);
  } else {
    // less common case - slower
    bitmap_clear_n(frames, frame_index, num_4k_pages);
  }

  fa->free += count * pagesize;
  SPIN_UNLOCK(&fa->lock);
}

static struct frame_allocator_impl bitmap_allocator = {
  .fa_init = bitmap_fa_init,
  .fa_alloc = bitmap_fa_alloc,
  .fa_reserve = bitmap_fa_reserve,
  .fa_free = bitmap_fa_free,
};

//
// MARK: frame allocator api
//

frame_allocator_t *new_frame_allocator(uintptr_t base, size_t size, struct frame_allocator_impl *impl) {
  frame_allocator_t *fa = kmallocz(sizeof(frame_allocator_t));
  fa->base = base;
  fa->size = size;
  fa->free = size;
  fa->impl = impl;
  spin_init(&fa->lock);

  fa->data = impl->fa_init(fa);
  if (fa->data == NULL) {
    kfree(fa);
    return NULL;
  }

  return fa;
}

page_t *fa_alloc_pages(frame_allocator_t *fa, size_t count, size_t pagesize) {
  ASSERT(pagesize == PAGE_SIZE || pagesize == PAGE_SIZE_2MB || pagesize == PAGE_SIZE_1GB);
  if (fa->free == 0 || count == 0) {
    return NULL;
  }

  // alloc the backing frames
  uintptr_t frame = fa->impl->fa_alloc(fa, count, pagesize);
  if (frame == 0) {
    return NULL;
  }

  ASSERT(is_aligned(frame, pagesize));
  return alloc_page_structs(fa, frame, count, pagesize, 0);
}

int fa_reserve_pages(frame_allocator_t *fa, uintptr_t frame, size_t count, size_t pagesize) {
  ASSERT(pagesize == PAGE_SIZE || pagesize == PAGE_SIZE_2MB || pagesize == PAGE_SIZE_1GB);
  ASSERT(is_aligned(frame, pagesize));
  ASSERT(frame >= fa->base && frame < fa->base + fa->size);
  if (fa->free == 0 || count == 0) {
    return -1;
  }

  if (fa->impl->fa_reserve(fa, frame, count, pagesize) < 0) {
    return -1;
  }
  return 0;
}

void fa_free_pages(page_t *pages) {
  if (pages == NULL) {
    return;
  }

  // since the list of pages doesnt have to be contiguous or homogenous we
  // need to make a separate call to the frame allocator for each contiguous
  // range of uniform pages.
  size_t count = 0;
  uintptr_t start_frame = pages->address;
  uintptr_t last_address;
  size_t last_pagesize;
  frame_allocator_t *last_fa = NULL;
  while (pages != NULL) {
    page_t *page = page_list_remove_head(&pages);
    ASSERT(page->mapping == NULL); // must be unmapped prior
    if (page->fa == NULL) {
      // unmanaged page just free the struct
      kfree(page);
      continue;
    }

    uintptr_t address = page->address;
    size_t pagesize = pg_flags_to_size(page->flags);
    if (count == 0) {
      // first page
      count++;
      goto free_struct;
    }

    if (address == last_address + last_pagesize && pagesize == last_pagesize) {
      // is contiguous and homogenous
      count++;
    } else {
      // free the range of pages up to the current and start new range
      if (last_fa)
        last_fa->impl->fa_free(last_fa, start_frame, count, last_pagesize);
      start_frame = page->address;
      count = 1;
    }

  LABEL(free_struct);
    last_address = address;
    last_pagesize = pagesize;
    last_fa = page->fa;
    kfree(page);
  }

  // free the last range of pages
  if (last_fa)
    last_fa->impl->fa_free(last_fa, start_frame, count, last_pagesize);
}

//
//

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
    zone_type_t type = get_mem_zone_type(base);
    zone_type_t end_type = get_mem_zone_type(base + size - 1);
    // kprintf("  [%018p-%018p] % 8zu %s\n", base, base+size, SIZE_TO_PAGES(size), zone_names[type]);
    if (type != end_type) {
      // an entry should never cross more than two zones
      kassert(end_type - type == 1);
      uintptr_t end_base = zone_limits[type];
      size_t end_size = base + size - end_base;

      // create allocator in the zone which this entry spills into
      frame_allocator_t *fa = new_frame_allocator(end_base, end_size, &bitmap_allocator);
      LIST_ADD(&mem_zones[end_type], fa, list);
      zone_page_count[end_type] += SIZE_TO_PAGES(end_size);

      // shrink original entry to end of zone
      size = fa->base - base;
    }

    frame_allocator_t *fa = new_frame_allocator(base, size, &bitmap_allocator);
    ASSERT(fa != NULL);
    LIST_ADD(&mem_zones[type], fa, list);
    zone_page_count[end_type] += SIZE_TO_PAGES(size);
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

int reserve_pages(uintptr_t address, size_t count, size_t pagesize) {
  kassert(is_aligned(address, pagesize));
  if (count == 0) {
    return -1;
  }

  size_t total_size = count * pagesize;
  zone_type_t type = get_mem_zone_type(address);
  zone_type_t end_type = get_mem_zone_type(address + total_size - 1);
  if (type != end_type) {
    panic("requested pages cross zone boundary");
  }

  // find allocator which contains the pages
  frame_allocator_t *fa = locate_owning_allocator(address);
  frame_allocator_t *fa_end = locate_owning_allocator(address + total_size - 1);
  if (fa != fa_end) {
    panic("requested pages cross zone sub-regions");
  }

  if (fa == NULL && fa_end == NULL) {
    // not managed by any allocators, the request is fine
    return 0;
  }
  return fa_reserve_pages(fa, address, count, pagesize);
}

//

page_t *alloc_pages_zone(zone_type_t zone_type, size_t count, size_t pagesize) {
  kassert(zone_type < MAX_ZONE_TYPE);
  if (count == 0) {
    return NULL;
  }

  page_t *pages = NULL;
  size_t total_size = count * pagesize;
  frame_allocator_t *fa = LIST_FIRST(&mem_zones[zone_type]);
  while (fa) {
    // try all of the allocators within the zone
    if (fa->free >= total_size) {
      pages = fa_alloc_pages(fa, count, pagesize);
      if (pages != NULL) {
        break;
      }
    }

    fa = LIST_NEXT(fa, list);
  }

  return pages;
}

page_t *alloc_pages_size(size_t count, size_t pagesize) {
  zone_type_t zone_type = ZONE_ALLOC_DEFAULT;

  page_t *pages = NULL;
  while (pages == NULL) {
    if (zone_type == MAX_ZONE_TYPE) {
      panic("out of memory");
    }

    // try all of the zones
    pages = alloc_pages_zone(zone_type, count, pagesize);
    zone_type = zone_alloc_order[zone_type];
  }

  return pages;
}

page_t *alloc_pages(size_t count) {
  return alloc_pages_size(count, PAGE_SIZE);
}

page_t *alloc_pages_at(uintptr_t address, size_t count, size_t pagesize) {
  if (reserve_pages(address, count, pagesize) < 0) {
    return NULL;
  }

  frame_allocator_t *fa = locate_owning_allocator(address);
  return alloc_page_structs(fa, address, count, pagesize, 0);
}

void free_pages(page_t *pages) {
  fa_free_pages(pages);
}

page_t *alloc_cow_page(page_t *page) {
  if (page == NULL) {
    return NULL;
  }
  return alloc_page_structs(NULL, page->address, 1, pg_flags_to_size(page->flags), PG_COW);
}

page_t *alloc_cow_pages_at(uintptr_t address, size_t count, size_t pagesize) {
  ASSERT(is_aligned(address, pagesize) && "address must be page-aligned");
  ASSERT(pagesize == PAGE_SIZE || pagesize == BIGPAGE_SIZE || pagesize == HUGEPAGE_SIZE);
  frame_allocator_t *fa = locate_owning_allocator(address);
  frame_allocator_t *fa_end = locate_owning_allocator(address+(count*pagesize)-1);
  if (fa != NULL || fa_end != NULL) {
    panic("alloc_cow_pages_at: part or all of requested range is managed by a frame allocator");
  }
  return alloc_page_structs(NULL, address, count, pagesize, PG_COW);
}

//

page_t *page_list_remove_head(page_t **list) {
  if (*list == NULL) {
    return NULL;
  }

  page_t *head = *list;
  page_t *next = head->next;
  ASSERT(head->flags & PG_HEAD);
  if (next) {
    next->flags |= PG_HEAD;
    next->head.count = head->head.count - 1;
  }
  // it stays a head, but with count 1
  head->head.count = 1;
  *list = next;
  return head;
}

page_t *page_list_add_tail(page_t *head, page_t *tail, page_t *page) {
  ASSERT(page->flags & PG_HEAD);
  if (head == NULL) {
    ASSERT(tail == NULL);
    return page;
  }

  ASSERT(head->flags & PG_HEAD);
  if (tail != head)
    ASSERT(!(tail->flags & PG_HEAD));

  tail->next = page;
  head->head.count += page->head.count;
  page->head.count = 0;
  page->flags &= ~PG_HEAD;
  return page;
}

bool mm_is_kernel_code_ptr(uintptr_t ptr) {
  return ptr >= kernel_code_start && ptr < kernel_code_end;
}

bool mm_is_kernel_data_ptr(uintptr_t ptr) {
  return ptr >= kernel_code_end && ptr < kernel_data_end;
}
