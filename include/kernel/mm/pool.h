//
// Created by Aaron Gill-Braun on 2025-08-16.
//

#ifndef KERNEL_MM_POOL_H
#define KERNEL_MM_POOL_H

#include <kernel/base.h>

struct pool_cache;
struct pool_slab;
struct pool_size_class;

/**
 * A pool allocator for fixed-size objects.
 *
 * When creating a pool, the caller provides a 0-terminated array of object sizes
 * that the pool will manage.
 */
typedef struct pool {
  struct pool_size_class *classes;  // array of size classes
  uint32_t num_classes;             // number of size classes
  uint32_t flags;                   // pool flags
  const char *name;                 // pool name
  size_t alignment;                 // allocation alignment requirement

  // tunable parameters
  uint32_t cache_capacity;          // objects per cache
  uint32_t reserve_max;             // max caches in reserve

  // statistics
  uint64_t allocs;
  uint64_t frees;
  uint64_t slab_creates;
  uint64_t slab_destroys;
} pool_t;

// pool flags
#define POOL_NOCACHE    0x01  // disable per-cpu caches
#define POOL_NOSTATS    0x02  // disable statistics tracking
#define POOL_LAZY       0x04  // lazy slab initialization

// api

#define pool_sizes(...) ((const size_t[]){__VA_ARGS__, 0})

/**
 * Create a pool allocator for fixed-size objects.
 * This function uses the default tuning parameters:
 *   alignment = 8 bytes
 *   cache_capacity = 64 objects per per-cpu cache
 *
 * @param name the name of the pool
 * @param sizes an array of object sizes managed by the pool (terminated by 0)
 * @param flags pool flags
 * @return a pointer to the created pool, or NULL on failure
 */
pool_t *pool_create(const char *name, const size_t *sizes, uint32_t flags);

/**
 * Create a pool allocator with custom tuning parameters.
 *
 * @param name the name of the pool
 * @param sizes an array of object sizes managed by the pool (terminated by 0)
 * @param flags pool flags
 * @param alignment the alignment requirement for allocations
 * @param cache_capacity the number of objects in each per-cpu cache
 * @param reserve_max the maximum number of reserve caches
 * @return a pointer to the created pool, or NULL on failure
 */
pool_t *pool_create_tune(const char *name, const size_t *sizes, uint32_t flags,
                         size_t alignment, size_t cache_capacity, size_t reserve_max);

/**
 * Destroy a pool allocator and free all associated resources.
 * No references to objects allocated from this pool should exist at the time of destruction
 * as the objects will no longer be valid.
 *
 * @param pool the pool to destroy
 */
void pool_destroy(pool_t *pool);

/**
 * Allocate an object from the pool.
 *
 * @param pool the pool to allocate from
 * @param size the size of the object to allocate
 * @return a pointer to the allocated object, or NULL on failure
 */
void *pool_alloc(pool_t *pool, size_t size);

/**
 * Free a pool-allocated object.
 *
 * @param pool the pool the object was allocated from
 * @param obj the object to free
 * @return the object pointer is set to NULL after freeing
 */
void pool_free(pool_t *pool, void *obj);

/**
 * Adjust the cache capacity for the pool.
 * This will change the number of objects each per-cpu cache can hold.
 * @param pool
 * @param capacity
 */
void pool_set_cache_capacity(pool_t *pool, uint32_t capacity);

/**
 * Adjust the maximum number of reserve caches for the pool.
 * @param pool
 * @param max
 */
void pool_set_reserve_max(pool_t *pool, uint32_t max);

/**
 * Preload the cache with objects for a specific size class.
 * This pre-allocates objects and populates a single reserve cache to reduce
 * allocation latency when the objects are first needed.
 *
 * @param pool the pool to preload
 * @param size the object size to preload (will match to appropriate size class)
 * @param count the number of objects to preload (must be <= cache_capacity)
 * @return the number of objects successfully preloaded, or 0 on error
 */
size_t pool_preload_cache(pool_t *pool, size_t size, size_t count);

/**
 * Print statistics about the pool to the kernel console.
 * This will return early if the pool was created with POOL_NOSTATS flag.
 * @param pool the pool
 */
void pool_print_debug_stats(pool_t *pool);

#endif
