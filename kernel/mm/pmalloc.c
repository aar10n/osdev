//
// Created by Aaron Gill-Braun on 2022-06-17.
//

#include <kernel/mm/pmalloc.h>
#include <kernel/mm/vmalloc.h>
#include <kernel/mm/init.h>

#include <kernel/mutex.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/panic.h>
#include <bitmap.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(x, ...) kprintf("pmalloc: " x, ##__VA_ARGS__)
//#define DPRINTF(x, ...)

#define ZONE_ALLOC_DEFAULT ZONE_TYPE_HIGH

static LIST_HEAD(frame_allocator_t) mem_zones[MAX_ZONE_TYPE];
static size_t zone_page_count[MAX_ZONE_TYPE];
static size_t reserved_pages = 128;
static page_t *initrd_pages = NULL;

static const size_t zone_limits[MAX_ZONE_TYPE] = {
  [ZONE_TYPE_LOW] = ZONE_LOW_MAX,
  [ZONE_TYPE_DMA] = ZONE_DMA_MAX,
  [ZONE_TYPE_NORMAL] = ZONE_NORMAL_MAX,
  [ZONE_TYPE_HIGH] = ZONE_HIGH_MAX
};
static const char *zone_names[MAX_ZONE_TYPE] = {
  [ZONE_TYPE_LOW] = "Low",
  [ZONE_TYPE_DMA] = "DMA",
  [ZONE_TYPE_NORMAL] = "Normal",
  [ZONE_TYPE_HIGH] = "High"
};

// this specifies the zone preference order for allocating pages
static zone_type_t zone_alloc_order[MAX_ZONE_TYPE] = {
  [ZONE_TYPE_LOW]     = MAX_ZONE_TYPE, // out of zones
  [ZONE_TYPE_DMA]     = ZONE_TYPE_LOW,
  [ZONE_TYPE_NORMAL]  = ZONE_TYPE_DMA,
  [ZONE_TYPE_HIGH]    = ZONE_TYPE_NORMAL,
};

static inline zone_type_t get_mem_zone_type(uintptr_t addr) {
  if (addr < ZONE_LOW_MAX) {
    return ZONE_TYPE_LOW;
  } else if (addr < ZONE_DMA_MAX) {
    return ZONE_TYPE_DMA;
  } else if (addr < ZONE_NORMAL_MAX) {
    return ZONE_TYPE_NORMAL;
  }
  return ZONE_TYPE_HIGH;
}

static frame_allocator_t *locate_owning_allocator(uintptr_t addr) {
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

__ref static page_t *alloc_page_structs(frame_allocator_t *fa, uintptr_t frame, size_t count, size_t pg_size) {
  ASSERT(count > 0);
  uint32_t pg_flags = PG_OWNING;
  if (pg_size == BIGPAGE_SIZE) {
    pg_flags |= PG_BIGPAGE;
  } else if (pg_size == HUGEPAGE_SIZE) {
    pg_flags |= PG_HUGEPAGE;
  }

  uintptr_t address = frame;
  size_t remaining = count;
  page_t *first = NULL;
  page_t *last = NULL;
  while (remaining > 0) {
    page_t *page = kmallocz(sizeof(page_t));
    page->address = address;
    page->flags = pg_flags;
    page->fa = fa;
    mtx_init(&page->pg_lock, MTX_SPIN, "pg_lock");
    initref(page);
    if (first == NULL) {
      first = moveref(page);
      last = first;
    } else {
      last->next = moveref(page);
      last = last->next;
    }

    address += pg_size;
    remaining--;
  }

  first->flags |= PG_HEAD;
  first->head.count = count;
  first->head.contiguous = true;
  return moveref(first);
}

__ref static page_t *alloc_nonowned_structs(uintptr_t frame, size_t count, size_t pg_size) {
  ASSERT(count > 0);
  ASSERT(frame % pg_size == 0);
  uint32_t pg_flags = 0;
  if (pg_size == BIGPAGE_SIZE) {
    pg_flags |= PG_BIGPAGE;
  } else if (pg_size == HUGEPAGE_SIZE) {
    pg_flags |= PG_HUGEPAGE;
  }

  uintptr_t address = frame;
  size_t remaining = count;
  page_t *first = NULL;
  page_t *last = NULL;
  while (remaining > 0) {
    page_t *page = kmallocz(sizeof(page_t));
    page->address = address;
    page->flags = pg_flags;
    mtx_init(&page->pg_lock, MTX_SPIN, "pg_lock");
    initref(page);
    if (first == NULL) {
      first = moveref(page);
      last = first;
    } else {
      last->next = moveref(page);
      last = last->next;
    }

    address += pg_size;
    remaining--;
  }

  first->flags |= PG_HEAD;
  first->head.count = count;
  first->head.contiguous = true;
  return moveref(first);
}

__ref static page_t *alloc_cow_structs(page_t *pages) {
  ASSERT(pages->flags & PG_HEAD);

  __ref page_t *first = NULL;
  page_t *last = NULL;
  page_t *curr = pages;
  while (curr) {
    page_t *page = kmallocz(sizeof(page_t));
    page->address = curr->address;
    page->flags = (curr->flags & PG_SIZE_MASK) | PG_COW;
    page->source = pg_getref(curr);
    mtx_init(&page->pg_lock, MTX_SPIN, "pg_lock");
    initref(page);
    if (first == NULL) {
      first = moveref(page);
      last = first;
    } else {
      last->next = moveref(page);
      last = last->next;
    }

    curr = curr->next;
  }

  first->flags |= PG_HEAD;
  first->head.count = pages->head.count;
  first->head.contiguous = pages->head.contiguous;
  return moveref(first);
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
    void *buffer = mm_early_map_pages_reserved(buffer_phys, num_bmp_pages, VM_WRITE|VM_NOCACHE);
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
  mtx_spin_lock(&fa->lock);
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
  mtx_spin_unlock(&fa->lock);
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
  mtx_spin_lock(&fa->lock);
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
  mtx_spin_unlock(&fa->lock);
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
  mtx_spin_lock(&fa->lock);
  if (num_4k_pages == 1) {
    // common case - fastest
    bitmap_clear(frames, frame_index);
  } else {
    // less common case - slower
    bitmap_clear_n(frames, frame_index, num_4k_pages);
  }

  fa->free += count * pagesize;
  mtx_spin_unlock(&fa->lock);
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
  mtx_init(&fa->lock, MTX_SPIN, "frame_allocator_lock");

  fa->data = impl->fa_init(fa);
  if (fa->data == NULL) {
    kfree(fa);
    return NULL;
  }

  return fa;
}

__move page_t *fa_alloc_pages(frame_allocator_t *fa, size_t count, size_t pg_size) {
  ASSERT(pg_size == PAGE_SIZE || pg_size == PAGE_SIZE_2MB || pg_size == PAGE_SIZE_1GB);
  if (fa->free == 0 || count == 0) {
    return NULL;
  }

  // alloc the backing frames
  uintptr_t frame = fa->impl->fa_alloc(fa, count, pg_size);
  if (frame == 0) {
    return NULL;
  }

  ASSERT(is_aligned(frame, pg_size));
  return alloc_page_structs(fa, frame, count, pg_size);
}

int fa_reserve_pages(frame_allocator_t *fa, uintptr_t frame, size_t count, size_t pg_size) {
  ASSERT(pg_size == PAGE_SIZE || pg_size == PAGE_SIZE_2MB || pg_size == PAGE_SIZE_1GB);
  ASSERT(is_aligned(frame, pg_size));
  ASSERT(frame >= fa->base && frame < fa->base + fa->size);
  if (fa->free == 0 || count == 0) {
    return -1;
  }

  return fa->impl->fa_reserve(fa, frame, count, pg_size);
}

void fa_free_page(page_t *page) {
  if (page->flags & PG_OWNING) {
    frame_allocator_t *fa = page->fa;
    fa->impl->fa_free(fa, page->address, 1, pg_flags_to_size(page->flags));
  } else if (page->flags & PG_COW) {
    // drop ref to the source page
    putref(&page->source, fa_free_page);
  }

  putref(&page->next, fa_free_page);
  kfree(page);
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
      ASSERT(end_type - type == 1);
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

  // reserve the initrd pages
  if (boot_info_v2->initrd_addr != 0) {
    size_t count = SIZE_TO_PAGES(boot_info_v2->initrd_size);
    if (reserve_pages(PG_RSRV_PHYS, boot_info_v2->initrd_addr, count, PAGE_SIZE) < 0) {
      panic("failed to reserve initrd pages");
    }
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

int reserve_pages(enum pg_rsrv_kind kind, uintptr_t address, size_t count, size_t pagesize) {
  ASSERT(pagesize == PAGE_SIZE || pagesize == PAGE_SIZE_2MB || pagesize == PAGE_SIZE_1GB);
  ASSERT(is_aligned(address, pagesize));
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
    if (kind != PG_RSRV_MANAGED) {
      return 0; // not managed by any allocators, the request is fine
    }
    return -1; // the request expects managed pages
  }

  if (kind == PG_RSRV_PHYS) {
    return -1; // the request expects unmanaged pages
  }

  return fa_reserve_pages(fa, address, count, pagesize);
}

//
// MARK: page allocation api
//

__ref page_t *alloc_pages_zone(zone_type_t zone_type, size_t count, size_t pagesize) {
  ASSERT(zone_type < MAX_ZONE_TYPE);
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

__ref page_t *alloc_pages_size(size_t count, size_t pagesize) {
  ASSERT(pagesize == PAGE_SIZE || pagesize == PAGE_SIZE_2MB || pagesize == PAGE_SIZE_1GB);
  zone_type_t zone_type = ZONE_ALLOC_DEFAULT;

  page_t *pages = NULL;
  while (pages == NULL) {
    if (zone_type == MAX_ZONE_TYPE) {
      panic("out of memory");
    }

    // try all of the zones
    pages = alloc_pages_zone(zone_type, count, pagesize);
    if (!pages)
      zone_type = zone_alloc_order[zone_type];
  }

  return pages;
}

__ref page_t *alloc_pages(size_t count) {
  return alloc_pages_size(count, PAGE_SIZE);
}

__ref page_t *alloc_pages_at(uintptr_t address, size_t count, size_t pagesize) {
  ASSERT(address % pagesize == 0);
  if (reserve_pages(PG_RSRV_ANY, address, count, pagesize) < 0) {
    return NULL;
  }

  frame_allocator_t *fa = locate_owning_allocator(address);
  return alloc_page_structs(fa, address, count, pagesize);
}

// non-owned pages are fake pages allocated to represent a region which is not
// actually backed by page_t pages (like for example physical mappings).
__ref page_t *alloc_nonowned_pages_at(uintptr_t address, size_t count, size_t pagesize) {
  ASSERT(address % pagesize == 0);
  if (count == 0) {
    return NULL;
  }

  // make sure the requests pages are actually unmanaged frames (therefore can be non-owned)
  if (reserve_pages(PG_RSRV_PHYS, address, count, pagesize) < 0) {
    panic("attempted to allocate unowned pages on an unreserved range");
  }
  return alloc_nonowned_structs(address, count, pagesize);
}

__ref page_t *alloc_cow_pages(page_t *pages) {
  return alloc_cow_structs(pages);
}

void _cleanup_pages(__move page_t **pagesref) {
  if (__expect_false(pagesref == NULL || *pagesref == NULL)) {
    return;
  }

  page_t *pages = moveref(*pagesref);
  putref(&pages, fa_free_page);
}

//
// MARK: Page list API
//

// when `count` is at or below this limit, the page list will allocate
// the backing array using kmalloc, otherwise it allocates pages.
#define PGLIST_MALLOC_LIMIT 512

static void page_list_alloc_array(page_t ***arrayref, uint32_t count, __out size_t *array_sz) {
  page_t **array = *arrayref;
  ASSERT(array == NULL);

  size_t total_size = count * sizeof(page_t *);
  if (count <= PGLIST_MALLOC_LIMIT) {
    // allocate the array on the kernel heap
    array = kmallocz(total_size);
    ASSERT(array != NULL);
  } else {
    // allocate the array on pages
    total_size = page_align(total_size);
    page_t *pages = alloc_pages(SIZE_TO_PAGES(total_size));
    ASSERT(pages != NULL);

    uintptr_t vaddr = vmap_pages(moveref(pages), 0, total_size, VM_RDWR|VM_NOCACHE, "page_list_array");
    ASSERT(vaddr != 0);

    memset((void *) vaddr, 0, total_size);
    array = (page_t **) vaddr;
  }
  *arrayref = array;
  *array_sz = total_size;
}

static void page_list_free_array(page_t ***arrayref, size_t count, size_t array_sz) {
  page_t **array = *arrayref;
  if (array == NULL) {
    return;
  }

  // drop all page references in the array
  for (uint32_t i = 0; i < count; i++) {
    pg_putref(&array[i]);
  }

  if (count <= PGLIST_MALLOC_LIMIT) {
    // the backing array lives on the kernel heap
    kfree(array);
    *arrayref = NULL;
  } else {
    vmap_free((uintptr_t) array, array_sz);
    *arrayref = NULL;
  }
}

/// Resizes the page list array to the new count and returns a linked list of pages
/// from the portion of the array that was removed.
static __ref page_t *page_list_resize_array(page_list_t *list, size_t new_count) {
  ASSERT(new_count > 0);
  if (new_count == list->count) {
    return NULL; // nothing to do
  }

  page_t **old_array = list->pages;
  size_t old_count = list->count;
  size_t old_size = list->array_sz;
  size_t new_size = new_count * sizeof(page_t *);
  if (new_count > PGLIST_MALLOC_LIMIT) {
    new_size = page_align(new_size);
  }

  if (new_size < old_size) {
    // only resize if the delta is significant
    if (new_size < old_size / 2) {
      // if new size is less than half of the old size, we can shrink the array
      list->pages = NULL;
      page_list_alloc_array(&list->pages, new_count, &list->array_sz);
      list->count = new_count;

      // copy the old pages to the new array
      for (size_t i = 0; i < old_count && i < new_count; i++) {
        list->pages[i] = moveref(old_array[i]);
      }
    } else {
      // otherwise just keep the old array and update the count
      list->count = new_count;
    }

    // collect the extra pages from the old array
    page_t *removed_pages = NULL;
    page_t *last_removed = NULL;
    for (size_t i = new_count; i < old_count; i++) {
      page_t *page = moveref(old_array[i]);
      page->flags &= ~PG_HEAD;
      if (removed_pages == NULL) {
        removed_pages = moveref(page);
        last_removed = removed_pages;
      } else {
        last_removed->next = moveref(page);
        last_removed = last_removed->next;
      }
    }

    if (removed_pages != NULL) {
      removed_pages->flags |= PG_HEAD; // mark the first page as head
      removed_pages->head.count = old_count - new_count; // set the count of removed pages
      removed_pages->head.contiguous = false; // todo: check if they are actually contiguous
    }
    return removed_pages;
  } else if (new_size > old_size) {
    // if the new size is larger than the old size, we need to allocate a new array
    list->pages = NULL;
    page_list_alloc_array(&list->pages, new_count, &list->array_sz);
    list->count = new_count;
    ASSERT(list->pages != NULL);

    // copy the old pages to the new array
    for (size_t i = 0; i < old_count && i < new_count; i++) {
      list->pages[i] = moveref(old_array[i]);
    }

    // free the old array if it was allocated on pages
    page_list_free_array(&old_array, old_count, old_size);
  }
  return NULL;
}

//

page_list_t *page_list_alloc_from(__ref page_t *pages) {
  ASSERT(pages->flags & PG_HEAD);
  page_list_t *list = kmallocz(sizeof(page_list_t));
  ASSERT(list != NULL);
  list->count = pages->head.count;
  page_list_alloc_array(&list->pages, list->count, &list->array_sz);

  // split up the page list and move them into the array
  size_t count = list->count;
  page_t *curr = moveref(pages);
  while (curr != NULL && count > 0) {
    page_t *next = moveref(curr->next);
    curr->next = NULL; // break the link to the next page
    curr->flags |= PG_HEAD;
    curr->head.count = 1; // each page is now a head page with count 1
    curr->head.contiguous = true;

    list->pages[list->count - count] = moveref(curr);
    curr = moveref(next);
    count--;
  }

  if (count > 0) {
    panic("page_list_alloc_from: not enough pages in the list");
  } else if (curr != NULL) {
    panic("page_list_alloc_from: too many pages in the list");
  }

  return list;
}

page_list_t *page_list_clone(page_list_t *list) {
  ASSERT(list->count > 0);
  ASSERT(list->pages != NULL);

  page_list_t *new_list = kmallocz(sizeof(page_list_t));
  ASSERT(new_list != NULL);
  new_list->count = list->count;
  page_list_alloc_array(&new_list->pages, new_list->count, &new_list->array_sz);

  // copy the pages from the old list to the new list
  for (size_t i = 0; i < list->count; i++) {
    if (list->pages[i] == NULL) {
      panic("page_list_clone: page at index %zu is NULL", i);
    }
    new_list->pages[i] = pg_getref(list->pages[i]);
  }
  return new_list;
}

void page_list_free(page_list_t **listref) {
  page_list_t *list = moveref(*listref);
  if (list == NULL) {
    return;
  }

  page_list_free_array(&list->pages, list->count, list->array_sz);
  kfree(list);
}

__ref page_t *page_list_getpage(page_list_t *list, size_t index) {
  ASSERT(index < list->count);

  page_t *page = list->pages[index];
  if (page == NULL) {
    panic("page_list_getpage: page at index %zu is NULL", index);
  }
  return pg_getref(page);
}

void page_list_putpage(page_list_t *list, size_t index, __ref page_t *page) {
  ASSERT(index < list->count);
  ASSERT(page != NULL);
  ASSERT(page->flags & PG_HEAD && page->head.count == 1);

  page_t *oldpage = list->pages[index];
  if (oldpage == NULL) {
    panic("page_list_putpage: page at index %zu is NULL", index);
  }

  pg_putref(&oldpage);
  list->pages[index] = moveref(page);
}

void page_list_join(page_list_t *head, page_list_t **tailref) {
  page_list_t *tail = moveref(*tailref);
  ASSERT(head != NULL);
  ASSERT(tail != NULL);
  ASSERT(head->count > 0 && tail->count > 0);
  ASSERT(head->pages != NULL && tail->pages != NULL);

  size_t head_count = head->count;
  size_t tail_count = tail->count;
  size_t new_count = head_count + tail_count;
  page_t *notail = page_list_resize_array(head, new_count);
  ASSERT(notail == NULL);

  // move the tail pages to the head list
  for (size_t i = head_count; i < new_count; i++) {
    size_t index = i - head_count;
    ASSERT(tail->pages[index] != NULL);
    head->pages[i] = moveref(tail->pages[index]);
  }

  // free the tail list
  page_list_free(&tail);
}

page_list_t *page_list_split(page_list_t *list, size_t index) {
  ASSERT(list != NULL);
  ASSERT(index < list->count);

  page_t *tail = page_list_resize_array(list, index);
  ASSERT(tail != NULL);
  return page_list_alloc_from(tail);
}

//


__ref page_t *raw_page_list_join(__ref page_t *head, __ref page_t *tail) {
  if (head == NULL) {
    return tail;
  } else if (tail == NULL) {
    return head;
  }

  ASSERT(head->flags & PG_HEAD);
  ASSERT(tail->flags & PG_HEAD);
  page_t *head_last = SLIST_GET_LAST(head, next);
  size_t head_size = pg_flags_to_size(head->flags);

  head->head.count += tail->head.count;
  head->head.contiguous = (head_last->address + head_size) == tail->address && tail->head.contiguous;

  tail->flags ^= PG_HEAD;
  tail->head.count = 0;
  tail->head.contiguous = false;
  head_last->next = moveref(tail);
  return head;
}
