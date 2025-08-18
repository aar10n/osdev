//
// Created by Aaron Gill-Braun on 2025-08-16.
//

#include <kernel/mm/pool.h>
#include <kernel/mm.h>
#include <kernel/mutex.h>
#include <kernel/queue.h>
#include <kernel/atomic.h>
#include <kernel/printf.h>
#include <kernel/panic.h>

#include <asm/bits.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("pool: " fmt, ##__VA_ARGS__)
#define EPRINTF(fmt, ...) kprintf("pool: %s: " fmt, __func__, ##__VA_ARGS__)

// per-cpu cache for lockless allocation
struct pool_cache {
  void **objects;              // array of object pointers
  uint32_t capacity;           // max objects
  uint32_t count;              // current objects
  LIST_ENTRY(struct pool_cache) reserve_link;
};

// slab structure for fixed-size objects
struct pool_slab {
  LIST_ENTRY(struct pool_slab) link;
  uintptr_t base;              // base address
  size_t size;                 // total slab size
  uint32_t obj_size;           // object size
  uint32_t obj_count;          // total objects
  uint32_t free_count;         // free objects
  void *freelist;              // free object list
  uint64_t *bitmap;            // allocation bitmap
};

// size class for a specific object size
struct pool_size_class {
  mtx_t lock;
  size_t obj_size;             // object size
  uint32_t slab_size;          // slab size in pages

  // slab lists
  LIST_HEAD(struct pool_slab) full_slabs;
  LIST_HEAD(struct pool_slab) partial_slabs;
  LIST_HEAD(struct pool_slab) empty_slabs;

  // per-cpu caches
  struct pool_cache **cpu_loaded;   // loaded cache per cpu
  struct pool_cache **cpu_prev;     // previous cache per cpu

  // cache reserve
  LIST_HEAD(struct pool_cache) reserve_full;
  LIST_HEAD(struct pool_cache) reserve_empty;
  uint32_t reserve_count;

  // stats
  uint64_t allocs;
  uint64_t frees;
  uint64_t cache_allocs;
  uint64_t cache_frees;
};

#define slab_bitmap_get(slab, idx) \
  (((slab)->bitmap[(idx) / 64] >> ((idx) % 64)) & 1)

#define slab_bitmap_set(slab, idx) \
  (slab)->bitmap[(idx) / 64] |= (1ULL << ((idx) % 64))

#define slab_bitmap_clear(slab, idx) \
  (slab)->bitmap[(idx) / 64] &= ~(1ULL << ((idx) % 64))

//
// MARK: Cache allocation
//

static struct pool_cache *cache_create(uint32_t capacity) {
  struct pool_cache *cache = kmallocz(sizeof(*cache));
  if (!cache) return NULL;

  cache->objects = kmalloc(capacity * sizeof(void *));
  if (!cache->objects) {
    kfree(cache);
    return NULL;
  }

  cache->capacity = capacity;
  cache->count = 0;
  return cache;
}

static void cache_destroy(struct pool_cache *cache) {
  kfree(cache->objects);
  kfree(cache);
}

static void *cache_obj_get(struct pool_cache *cache) {
  if (cache->count == 0) return NULL;
  return cache->objects[--cache->count];
}

static bool cache_obj_put(struct pool_cache *cache, void *obj) {
  if (cache->count >= cache->capacity) return false;
  cache->objects[cache->count++] = obj;
  return true;
}

// allocate an object from a cpu cache
static void *cache_alloc(pool_t *pool, struct pool_size_class *class) {
  uint8_t cpu = curcpu_id;
  struct pool_cache *loaded = class->cpu_loaded[cpu];

  // try loaded cache
  if (loaded) {
    void *obj = cache_obj_get(loaded);
    if (obj) {
      if (!(pool->flags & POOL_NOSTATS))
        atomic_fetch_add(&class->cache_allocs, 1);
      return obj;
    }
  }

  // try previous cache
  struct pool_cache *prev = class->cpu_prev[cpu];
  if (prev && prev->count > 0) {
    class->cpu_loaded[cpu] = prev;
    class->cpu_prev[cpu] = loaded;
    void *obj = cache_obj_get(prev);
    if (!(pool->flags & POOL_NOSTATS))
      atomic_fetch_add(&class->cache_allocs, 1);
    return obj;
  }

  // get full cache from reserve
  mtx_lock(&class->lock);
  struct pool_cache *full = LIST_FIRST(&class->reserve_full);
  if (full) {
    LIST_REMOVE(&class->reserve_full, full, reserve_link);
    class->reserve_count--;
    mtx_unlock(&class->lock);

    // install new cache
    if (loaded && loaded->count == 0) {
      LIST_ADD(&class->reserve_empty, loaded, reserve_link);
      class->reserve_count++;
    }
    class->cpu_loaded[cpu] = full;

    void *obj = cache_obj_get(full);
    if (!(pool->flags & POOL_NOSTATS))
      atomic_fetch_add(&class->cache_allocs, 1);
    return obj;
  }
  mtx_unlock(&class->lock);

  return NULL;
}

// free an object to a cpu cache
static bool cache_free(pool_t *pool, struct pool_size_class *class, void *obj) {
  uint8_t cpu = curcpu_id;
  struct pool_cache *loaded = class->cpu_loaded[cpu];

  // try loaded cache
  if (loaded && cache_obj_put(loaded, obj)) {
    if (!(pool->flags & POOL_NOSTATS))
      atomic_fetch_add(&class->cache_frees, 1);
    return true;
  }

  // try previous cache
  struct pool_cache *prev = class->cpu_prev[cpu];
  if (prev && cache_obj_put(prev, obj)) {
    if (!(pool->flags & POOL_NOSTATS))
      atomic_fetch_add(&class->cache_frees, 1);
    return true;
  }

  // loaded is full, exchange with reserve
  if (loaded && loaded->count == loaded->capacity) {
    mtx_lock(&class->lock);
    if (class->reserve_count < pool->reserve_max) {
      LIST_ADD(&class->reserve_full, loaded, reserve_link);
      class->reserve_count++;

      // get empty cache
      struct pool_cache *empty = LIST_FIRST(&class->reserve_empty);
      if (empty) {
        LIST_REMOVE(&class->reserve_empty, empty, reserve_link);
        class->reserve_count--;
        class->cpu_loaded[cpu] = empty;
        mtx_unlock(&class->lock);

        cache_obj_put(empty, obj);
        if (!(pool->flags & POOL_NOSTATS))
          atomic_fetch_add(&class->cache_frees, 1);
        return true;
      } else {
        // allocate new cache
        class->cpu_loaded[cpu] = NULL;
        mtx_unlock(&class->lock);

        struct pool_cache *new_cache = cache_create(pool->cache_capacity);
        if (new_cache) {
          class->cpu_loaded[cpu] = new_cache;
          cache_obj_put(new_cache, obj);
          if (!(pool->flags & POOL_NOSTATS))
            atomic_fetch_add(&class->cache_frees, 1);
          return true;
        }
      }
    } else {
      mtx_unlock(&class->lock);
    }
  }

  return false;
}

//
// MARK: Slab allocation
//

// create a new slab for the given size class
static struct pool_slab *slab_create(pool_t *pool, struct pool_size_class *class) {
  size_t slab_pages = class->slab_size;
  __ref page_t *pages = alloc_pages(slab_pages);
  if (!pages) return NULL;

  uintptr_t base = vmap_pages(pages, 0, slab_pages * PAGE_SIZE, VM_RDWR, "pool_slab");
  if (!base) {
    pg_putref(&pages);
    return NULL;
  }

  struct pool_slab *slab = (struct pool_slab *)base;
  slab->base = base;
  slab->size = slab_pages * PAGE_SIZE;
  slab->obj_size = class->obj_size;

  // calculate object layout with alignment
  size_t obj_align = pool->alignment ? pool->alignment : 8;
  size_t header_size = align(sizeof(*slab), obj_align);
  size_t bitmap_size = align((slab->size - header_size) / class->obj_size / 8, 8);
  size_t obj_start = align(header_size + bitmap_size, obj_align);
  size_t usable = slab->size - obj_start;
  slab->obj_count = usable / class->obj_size;
  slab->bitmap = offset_ptr(slab, header_size);

  // build freelist
  void *obj_base = offset_ptr(slab, obj_start);
  slab->freelist = NULL;
  for (int i = (int)slab->obj_count - 1; i >= 0; i--) {
    void *obj = offset_ptr(obj_base, i * class->obj_size);
    *(void **)obj = slab->freelist;
    slab->freelist = obj;
  }
  slab->free_count = slab->obj_count;

  return slab;
}

// destroy a slab
static void slab_destroy(struct pool_slab *slab) {
  vmap_free(slab->base, slab->size);
}

// allocate an object from slab
static void *slab_alloc(pool_t *pool, struct pool_slab *slab) {
  if (!slab->freelist) return NULL;

  void *obj = slab->freelist;
  slab->freelist = *(void **)obj;
  slab->free_count--;

  // mark in bitmap
  size_t obj_align = pool->alignment ? pool->alignment : 8;
  size_t header_size = align(sizeof(*slab), obj_align);
  size_t bitmap_size = align(slab->obj_count / 8, 8);
  size_t obj_start = align(header_size + bitmap_size, obj_align);
  void *obj_base = offset_ptr(slab, obj_start);
  size_t index = (offset_addr(obj, 0) - offset_addr(obj_base, 0)) / slab->obj_size;
  slab_bitmap_set(slab, index);
  __bts64(slab->bitmap + (index / 64), index % 64);
  return obj;
}

// free an object back to slab
static void slab_free(pool_t *pool, struct pool_slab *slab, void *obj) {
  size_t obj_align = pool->alignment ? pool->alignment : 8;
  size_t header_size = align(sizeof(*slab), obj_align);
  size_t bitmap_size = align(slab->obj_count / 8, 8);
  size_t obj_start = align(header_size + bitmap_size, obj_align);
  void *obj_base = offset_ptr(slab, obj_start);
  size_t index = (offset_addr(obj, 0) - offset_addr(obj_base, 0)) / slab->obj_size;

  ASSERT(slab_bitmap_get(slab, index));
  slab_bitmap_clear(slab, index);

  *(void **)obj = slab->freelist;
  slab->freelist = obj;
  slab->free_count++;
}

// locate the slab that an object belongs to and get its size class
static struct pool_size_class *find_obj_class(pool_t *pool, void *obj) {
  uintptr_t addr = (uintptr_t)obj;

  for (uint32_t i = 0; i < pool->num_classes; i++) {
    struct pool_size_class *class = &pool->classes[i];
    LIST_FOR_IN(slab, &class->full_slabs, link) {
      if (addr >= slab->base && addr < slab->base + slab->size)
        return class;
    }
    LIST_FOR_IN(slab, &class->partial_slabs, link) {
      if (addr >= slab->base && addr < slab->base + slab->size)
        return class;
    }
  }
  return NULL;
}

//
// MARK: Backend allocation
//

static void *backend_alloc(pool_t *pool, struct pool_size_class *class) {
  mtx_lock(&class->lock);

  // try partial slabs first
  struct pool_slab *slab = LIST_FIRST(&class->partial_slabs);
  if (slab) {
    void *obj = slab_alloc(pool, slab);
    if (obj) {
      if (slab->free_count == 0) {
        LIST_REMOVE(&class->partial_slabs, slab, link);
        LIST_ADD(&class->full_slabs, slab, link);
      }
      if (!(pool->flags & POOL_NOSTATS))
        atomic_fetch_add(&class->allocs, 1);
      mtx_unlock(&class->lock);
      return obj;
    }
  }

  // try empty slabs
  slab = LIST_FIRST(&class->empty_slabs);
  if (slab) {
    void *obj = slab_alloc(pool, slab);
    if (obj) {
      LIST_REMOVE(&class->empty_slabs, slab, link);
      LIST_ADD(&class->partial_slabs, slab, link);
      if (!(pool->flags & POOL_NOSTATS))
        atomic_fetch_add(&class->allocs, 1);
      mtx_unlock(&class->lock);
      return obj;
    }
  }

  // allocate new slab
  mtx_unlock(&class->lock);
  slab = slab_create(pool, class);
  if (!slab) return NULL;

  if (!(pool->flags & POOL_NOSTATS))
    atomic_fetch_add(&pool->slab_creates, 1);

  mtx_lock(&class->lock);
  void *obj = slab_alloc(pool, slab);
  LIST_ADD(&class->partial_slabs, slab, link);
  if (!(pool->flags & POOL_NOSTATS))
    atomic_fetch_add(&class->allocs, 1);
  mtx_unlock(&class->lock);

  return obj;
}

static void backend_free(pool_t *pool, struct pool_size_class *class, void *obj) {
  mtx_lock(&class->lock);

  // find slab containing object
  struct pool_slab *slab = NULL;
  LIST_FOREACH(slab, &class->full_slabs, link) {
    if ((uintptr_t)obj >= slab->base && (uintptr_t)obj < slab->base + slab->size)
      break;
  }
  if (!slab) {
    LIST_FOREACH(slab, &class->partial_slabs, link) {
      if ((uintptr_t)obj >= slab->base && (uintptr_t)obj < slab->base + slab->size)
        break;
    }
  }

  if (!slab) {
    mtx_unlock(&class->lock);
    EPRINTF("object %p not found in any slab\n", obj);
    return;
  }

  bool was_full = (slab->free_count == 0);
  slab_free(pool, slab, obj);

  if (was_full) {
    LIST_REMOVE(&class->full_slabs, slab, link);
    LIST_ADD(&class->partial_slabs, slab, link);
  } else if (slab->free_count == slab->obj_count) {
    LIST_REMOVE(&class->partial_slabs, slab, link);
    LIST_ADD(&class->empty_slabs, slab, link);
  }

  if (!(pool->flags & POOL_NOSTATS))
    atomic_fetch_add(&class->frees, 1);
  mtx_unlock(&class->lock);
}

static uint32_t calc_slab_size(size_t obj_size) {
  // determine optimal slab size for object size
  if (obj_size <= 64) return 1;        // 4KB slab
  if (obj_size <= 256) return 2;       // 8KB slab
  if (obj_size <= 1024) return 4;      // 16KB slab
  if (obj_size <= 4096) return 8;      // 32KB slab
  if (obj_size <= 16384) return 16;    // 64KB slab
  return 32;                           // 128KB slab
}

//
// MARK: API Implementation
//

pool_t *pool_create(const char *name, const size_t *sizes, uint32_t flags) {
  return pool_create_tune(name, sizes, flags, 0, 64, 16);
}

pool_t *pool_create_tune(const char *name, const size_t *sizes, uint32_t flags,
                         size_t alignment, size_t cache_capacity, size_t reserve_max) {
  // count sizes
  uint32_t num_sizes = 0;
  while (sizes[num_sizes] != 0)
    num_sizes++;

  if (num_sizes == 0 || num_sizes > 64) {
    EPRINTF("invalid number of size classes: %u\n", num_sizes);
    return NULL;
  }

  pool_t *pool = kmallocz(sizeof(*pool));
  if (!pool)
    return NULL;

  pool->name = name;
  pool->num_classes = num_sizes;
  pool->flags = flags;
  pool->alignment = alignment;
  pool->cache_capacity = cache_capacity;
  pool->reserve_max = reserve_max;

  pool->classes = kmallocz(num_sizes * sizeof(struct pool_size_class));
  if (!pool->classes) {
    kfree(pool);
    return NULL;
  }

  // initialize size classes
  for (uint32_t i = 0; i < num_sizes; i++) {
    struct pool_size_class *class = &pool->classes[i];
    class->obj_size = align(sizes[i], 8);
    class->slab_size = calc_slab_size(class->obj_size);

    LIST_INIT(&class->full_slabs);
    LIST_INIT(&class->partial_slabs);
    LIST_INIT(&class->empty_slabs);
    LIST_INIT(&class->reserve_full);
    LIST_INIT(&class->reserve_empty);

    mtx_init(&class->lock, 0, "pool_class");

    // allocate per-cpu caches if not disabled
    if (!(flags & POOL_NOCACHE)) {
      uint32_t ncpus = 256;  // max cpus
      class->cpu_loaded = kmallocz(ncpus * sizeof(struct pool_cache *));
      class->cpu_prev = kmallocz(ncpus * sizeof(struct pool_cache *));

      if (!class->cpu_loaded || !class->cpu_prev) {
        // cleanup on failure
        for (uint32_t j = 0; j <= i; j++) {
          kfree(pool->classes[j].cpu_loaded);
          kfree(pool->classes[j].cpu_prev);
          mtx_destroy(&pool->classes[j].lock);
        }
        kfree(pool->classes);
        kfree(pool);
        return NULL;
      }
    }

    // pre-allocate initial slab if not lazy
    if (!(flags & POOL_LAZY)) {
      struct pool_slab *slab = slab_create(pool, class);
      if (slab) {
        LIST_ADD(&class->empty_slabs, slab, link);
        if (!(pool->flags & POOL_NOSTATS))
          atomic_fetch_add(&pool->slab_creates, 1);
      }
    }
  }

  return pool;
}

void pool_destroy(pool_t *pool) {
  ASSERT(pool != NULL);

  // destroy all slabs and magazines
  for (uint32_t i = 0; i < pool->num_classes; i++) {
    struct pool_size_class *class = &pool->classes[i];

    // destroy slabs
    LIST_FOR_IN_SAFE(slab, &class->full_slabs, link) {
      slab_destroy(slab);
      atomic_fetch_add(&pool->slab_destroys, 1);
    }
    LIST_FOR_IN_SAFE(slab, &class->partial_slabs, link) {
      slab_destroy(slab);
      atomic_fetch_add(&pool->slab_destroys, 1);
    }
    LIST_FOR_IN_SAFE(slab, &class->empty_slabs, link) {
      slab_destroy(slab);
      atomic_fetch_add(&pool->slab_destroys, 1);
    }

    // destroy caches
    if (!(pool->flags & POOL_NOCACHE)) {
      uint32_t ncpus = 256;
      for (uint32_t cpu = 0; cpu < ncpus; cpu++) {
        if (class->cpu_loaded[cpu])
          cache_destroy(class->cpu_loaded[cpu]);
        if (class->cpu_prev[cpu])
          cache_destroy(class->cpu_prev[cpu]);
      }

      LIST_FOR_IN(cache, &class->reserve_full, reserve_link) {
        cache_destroy(cache);
      }
      LIST_FOR_IN(cache, &class->reserve_empty, reserve_link) {
        cache_destroy(cache);
      }

      kfree(class->cpu_loaded);
      kfree(class->cpu_prev);
    }

    mtx_destroy(&class->lock);
  }

  kfree(pool->classes);
  kfree(pool);
}

void *pool_alloc(pool_t *pool, size_t size) {
  // find matching size class
  struct pool_size_class *class = NULL;
  for (uint32_t i = 0; i < pool->num_classes; i++) {
    if (pool->classes[i].obj_size >= size) {
      class = &pool->classes[i];
      break;
    }
  }

  if (!class) {
    EPRINTF("invalid allocation size: %zu for pool '%s'\n", size, pool->name);
    return NULL;
  }

  void *obj = NULL;

  if (!(pool->flags & POOL_NOCACHE)) {
    // try the per-cpu pool cache first
    obj = cache_alloc(pool, class);
    if (obj) {
      if (!(pool->flags & POOL_NOSTATS))
        atomic_fetch_add(&pool->allocs, 1);
      return obj;
    }
  }

  // otherwise, allocate from the slab
  obj = backend_alloc(pool, class);
  if (obj) {
    if (!(pool->flags & POOL_NOSTATS))
      atomic_fetch_add(&pool->allocs, 1);
  }

  return obj;
}

void pool_free(pool_t *pool, void *obj) {
  if (!obj) return;

  // find matching size class by searching all slabs
  struct pool_size_class *class = find_obj_class(pool, obj);
  if (!class) {
    EPRINTF("object %p not found in any size class\n", obj);
    return;
  }

  // try cache path first
  if (!(pool->flags & POOL_NOCACHE)) {
    if (cache_free(pool, class, obj)) {
      if (!(pool->flags & POOL_NOSTATS))
        atomic_fetch_add(&pool->frees, 1);
      return;
    }
  }

  // fallback to backend
  backend_free(pool, class, obj);
  if (!(pool->flags & POOL_NOSTATS))
    atomic_fetch_add(&pool->frees, 1);
}

// tuning functions

void pool_set_cache_capacity(pool_t *pool, uint32_t capacity) {
  pool->cache_capacity = capacity;
}

void pool_set_reserve_max(pool_t *pool, uint32_t max) {
  pool->reserve_max = max;
}

size_t pool_preload_cache(pool_t *pool, size_t size, size_t count) {
  // caching must be enabled
  if (pool->flags & POOL_NOCACHE) {
    return 0;
  }

  // count must not exceed cache capacity
  if (count > pool->cache_capacity) {
    EPRINTF("count %zu exceeds cache capacity %u\n", count, pool->cache_capacity);
    return 0;
  }

  // find matching size class
  struct pool_size_class *class = NULL;
  for (uint32_t i = 0; i < pool->num_classes; i++) {
    if (pool->classes[i].obj_size >= size) {
      class = &pool->classes[i];
      break;
    }
  }

  if (!class) {
    EPRINTF("invalid size: %zu for pool '%s'\n", size, pool->name);
    return 0;
  }

  // check if reserve is full
  mtx_lock(&class->lock);
  if (class->reserve_count >= pool->reserve_max) {
    mtx_unlock(&class->lock);
    return 0;
  }
  mtx_unlock(&class->lock);

  // create a single cache
  struct pool_cache *cache = cache_create(pool->cache_capacity);
  if (!cache) {
    return 0;
  }

  // fill the cache with objects
  size_t preloaded = 0;
  for (size_t i = 0; i < count; i++) {
    void *obj = backend_alloc(pool, class);
    if (!obj) {
      break;
    }
    cache_obj_put(cache, obj);
    preloaded++;
  }

  // add cache to reserve if we got any objects
  if (preloaded > 0) {
    mtx_lock(&class->lock);
    if (class->reserve_count < pool->reserve_max) {
      LIST_ADD(&class->reserve_full, cache, reserve_link);
      class->reserve_count++;
    } else {
      // reserve is now full, return objects to backend
      mtx_unlock(&class->lock);
      while (cache->count > 0) {
        void *obj = cache_obj_get(cache);
        backend_free(pool, class, obj);
      }
      cache_destroy(cache);
      return 0;
    }
    mtx_unlock(&class->lock);
  } else {
    cache_destroy(cache);
  }

  return preloaded;
}

// debugging functions

void pool_print_debug_stats(pool_t *pool) {
  if (pool->flags & POOL_NOSTATS) {
    kprintf("pool '%s' statistics are disabled\n", pool->name);
    return;
  }

  kprintf_raw("pool '%s' statistics:\n", pool->name);
  kprintf_raw("  total allocs: %llu\n", pool->allocs);
  kprintf_raw("  total frees: %llu\n", pool->frees);
  kprintf_raw("  slabs created: %llu\n", pool->slab_creates);
  kprintf_raw("  slabs destroyed: %llu\n", pool->slab_destroys);
  kprintf_raw("  flags: %s%s%s\n",
          (pool->flags & POOL_NOCACHE) ? "NOCACHE " : "",
          (pool->flags & POOL_NOSTATS) ? "NOSTATS " : "",
          (pool->flags & POOL_LAZY) ? "LAZY" : "");
  kprintf_raw("  alignment: %zu\n", pool->alignment);

  for (uint32_t i = 0; i < pool->num_classes; i++) {
    struct pool_size_class *class = &pool->classes[i];
    kprintf_raw("  size class %zu:\n", class->obj_size);
    kprintf_raw("    backend allocs: %llu\n", class->allocs);
    kprintf_raw("    backend frees: %llu\n", class->frees);

    if (!(pool->flags & POOL_NOCACHE)) {
      kprintf_raw("    cache allocs: %llu\n", class->cache_allocs);
      kprintf_raw("    cache frees: %llu\n", class->cache_frees);
      kprintf_raw("    reserve caches: %u\n", class->reserve_count);
    }

    // count slabs
    uint32_t full = 0, partial = 0, empty = 0;
    LIST_FOR_IN(slab, &class->full_slabs, link) full++;
    LIST_FOR_IN(slab, &class->partial_slabs, link) partial++;
    LIST_FOR_IN(slab, &class->empty_slabs, link) empty++;

    kprintf_raw("    slabs: %u full, %u partial, %u empty\n", full, partial, empty);
  }
}
