//
// Created by Aaron Gill-Braun on 2023-12-22.
//

#include <kernel/lock.h>
#include <kernel/proc.h>
#include <kernel/tqueue.h>
#include <kernel/panic.h>
#include <kernel/atomic.h>
#include <kernel/printf.h>

#define ASSERT(x) kassert(x)

#define MAX_CLAIMS 8

/*
 * A lock claim is a record of a lock held by a thread.
 * It is used to track the locks that a thread owns and lives in a lock_claim_list.
 */
struct lock_claim {
  struct lock_object *lock; // the owned lock
  uintptr_t how;            // how the lock was acquired
  const char *file;         // file where the lock was acquired
  int line;                 // line where the lock was acquired
};

/*
 * A list of lock_claims.
 * It is used to hold a number of lock claims and avoid allocations in the locking
 * path which results in a new claim being written. We only need to allocate when
 * the per-node list is full.
 */
struct lock_claim_list {
  struct lock_claim claims[MAX_CLAIMS];// list of claims
  int nclaims;                  // number of claims
  struct lock_claim_list *next; // next list
};

// MARK: lock claims

struct lock_claim_list *lock_claim_list_alloc() {
  struct lock_claim_list *list = kmallocz(sizeof(struct lock_claim_list));
  list->nclaims = 0;
  list->next = NULL;
  return list;
}

void lock_claim_list_free(struct lock_claim_list **listp) {
  struct lock_claim_list *list = *listp;
  while (list) {
    struct lock_claim_list *next = list->next;
    kfree(list);
    list = next;
  }
  *listp = NULL;
}

void lock_claim_list_add(struct lock_claim_list *list, struct lock_object *lock, uintptr_t how, const char *file, int line) {
  if (list->nclaims == MAX_CLAIMS) {
    struct lock_claim_list *next = lock_claim_list_alloc();
    list->next = next;
    list = next;
  }

  struct lock_claim *claim = &list->claims[list->nclaims++];
  claim->lock = lock;
  claim->how = how;
  claim->file = file;
  claim->line = line;
}

void lock_claim_list_remove(struct lock_claim_list *list, struct lock_object *lock) {
  // get the last list in the chain
  while (list->nclaims == MAX_CLAIMS && list->next) {
    list = list->next;
  }

  // scan in reverse order to find the most recent claim
  for (int i = list->nclaims - 1; i >= 0; i--) {
    if (list->claims[i].lock == lock) {
      list->claims[i].lock = NULL;
      list->nclaims--;
      return;
    }
  }

  // if we get here then the lock was not found
  panic("lock_claim_list_remove() on unowned lock");
}

//

static void percpu_early_init_claim_list() {
  // this per-cpu lock claim list tracks spin lock claims
  struct lock_claim_list *list = lock_claim_list_alloc();
  PERCPU_AREA->spinlocks = list;
}
PERCPU_EARLY_INIT(percpu_early_init_claim_list);


// =================================
// MARK: mutex
// =================================

#define MTX_ASSERT(x, fmt, ...) kassertf(x, fmt, __VA_ARGS__)
#define MTX_DEBUGF(m, fmt, ...) \
  if (__expect_false(mtx_get_opts(m) & MTX_DEBUG)) kprintf("mutex: " fmt "", __VA_ARGS__)

#define mtx_set_owner(m, td) (mtx->owner_opts = (uintptr_t)(td) | ((mtx)->owner_opts & MTX_OPT_MASK))
#define mtx_set_opts(m, opts) (mtx->owner_opts = (opts) | ((mtx)->owner_opts & ~MTX_OPT_MASK))
#define mtx_get_owner(m) ((thread_t *) ((mtx)->owner_opts & ~MTX_OPT_MASK))
#define mtx_get_opts(m) ((mtx)->owner_opts & MTX_OPT_MASK)

// mutex state
#define MTX_UNOWNED   0x00 // free mutex state
#define MTX_LOCKED    0x01 // mutex is locked
#define MTX_RECURSED  0x02 // mutex is locked recursively (non-spin)
#define MTX_DESTROYED 0x04 // mutex has been destroyed (non-spin)

static inline void spinlock_enter() {
  if (atomic_fetch_add(&curthread->spin_count, 1) == 0)
    critical_enter();
}

static inline void spinlock_exit() {
  MTX_ASSERT(curthread->spin_count > 0, "spinlock_exit() with no spin locks held");
  if (atomic_fetch_sub(&curthread->spin_count, 1) == 1)
    critical_exit();
}

/////////////////////////
// MARK: mtx_spin_lock

int _mtx_spin_trylock(mtx_t *mtx, const char *file, int line) {
  MTX_ASSERT(mtx->lo.state != MTX_DESTROYED, "_mtx_spin_trylock() on destroyed mutex, %s:%d", file, line);

  spinlock_enter();
  if (atomic_cmpxchg_acq(&mtx->lo.state, MTX_UNOWNED, MTX_LOCKED) == MTX_UNOWNED) {
    mtx_set_owner(mtx, curthread);
    mtx->lo.data = 1;
    return 1;
  }
  spinlock_exit();
  return 0;
}

void _mtx_spin_lock(mtx_t *mtx, const char *file, int line) {
  MTX_ASSERT(mtx->lo.state != MTX_DESTROYED, "_mtx_spin_lock() on destroyed mutex, %s:%d", file, line);
  MTX_ASSERT(!mtx_owned(mtx), "_mtx_spin_lock() on owned mutex, %s:%d", file, line);

  spinlock_enter();
  for (;;) {
    // https://rigtorp.se/spinlock/
    // test and test-and-set lock optimize for uncontended case
    if (atomic_cmpxchg_acq(&mtx->lo.state, MTX_UNOWNED, MTX_LOCKED)) {
      break;
    }
    while (atomic_load_relaxed(&mtx->lo.state) != MTX_UNOWNED) {
      cpu_pause();
    }
  }

  mtx_set_owner(mtx, curthread);
  mtx->lo.data = 1;
}

void _mtx_spin_unlock(mtx_t *mtx) {
  MTX_ASSERT(mtx->lo.state != MTX_DESTROYED, "_mtx_spin_unlock() on destroyed mutex");
  MTX_ASSERT(mtx_owned(mtx), "_mtx_spin_unlock() on unowned mutex");

  mtx->lo.data = 0;
  atomic_store_release(&mtx->lo.state, MTX_UNOWNED);
  spinlock_exit();
}

/////////////////////////
// MARK: mtx_wait_lock

int _mtx_wait_trylock(mtx_t *mtx, const char *file, int line) {
  MTX_ASSERT(mtx->lo.state != MTX_DESTROYED, "_mtx_wait_trylock() on destroyed mutex, %s:%d", file, line);
  MTX_ASSERT(!(mtx_get_opts(mtx) & MTX_SPIN), "_mtx_wait_trylock() on spin mutex, %s:%d", file, line);

  if (mtx_owned(mtx)) {
    MTX_ASSERT(mtx_get_opts(mtx) & MTX_RECURSE, "_mtx_wait_trylock() on non-recursive mutex, %s:%d", file, line);
    mtx->lo.data++;
    curthread->lock_count++;
    return 1;
  } else if (atomic_cmpxchg_acq(&mtx->lo.state, MTX_UNOWNED, MTX_LOCKED) == MTX_UNOWNED) {
    mtx_set_owner(mtx, curthread);
    mtx->lo.data = 1;
    curthread->lock_count++;
    return 1;
  }
  return 0;
}

void _mtx_wait_lock(mtx_t *mtx, const char *file, int line) {
  MTX_ASSERT(mtx->lo.state != MTX_DESTROYED, "_mtx_wait_lock() on destroyed mutex, %s:%d", file, line);
  MTX_ASSERT(!(mtx_get_opts(mtx) & MTX_SPIN), "_mtx_wait_lock() on spin mutex, %s:%d", file, line);

  for (;;) {

  }
}

void _mtx_wait_unlock(mtx_t *mtx) {
  todo();
}

//

void _mtx_init(mtx_t *mtx, uint32_t opts, const char *name) {
  mtx->lo.name = name;
  mtx->lo.state = MTX_UNOWNED;
  mtx->lo.data = 0; // recurse count
  mtx->owner_opts = opts & MTX_OPT_MASK;
}

void _mtx_destroy(mtx_t *mtx) {
  todo();
}

int _mtx_locked(mtx_t *mtx) {
  return atomic_load_relaxed(&mtx->lo.state) == MTX_LOCKED;
}

int _mtx_owned(mtx_t *mtx) {
  return atomic_load_relaxed(&mtx->lo.state) == MTX_LOCKED && mtx_get_owner(mtx) == curthread;
}

int _mtx_assert(mtx_t *mtx, int what, const char *file, int line) {
  if (what == MA_OWNED) {
    MTX_ASSERT(mtx_owned(mtx), "_mtx_assert() on unowned mutex, %s:%d", file, line);
  } else if (what == MA_NOTOWNED) {
    MTX_ASSERT(!mtx_owned(mtx), "_mtx_assert() on owned mutex, %s:%d", file, line);
  } else if (what == MA_RECURSED) {
    MTX_ASSERT(mtx_owned(mtx) && mtx->lo.data > 0, "_mtx_assert() on unowned mutex, %s:%d", file, line);
  } else {
    panic("invalid mutex assert option: %d", what);
  }
  return 1;
}

int _mtx_trylock(mtx_t *mtx, const char *file, int line) {
  if (mtx_get_opts(mtx) & MTX_SPIN) {
    return _mtx_spin_trylock(mtx, file, line);
  } else {
    return _mtx_wait_trylock(mtx, file, line);
  }
}

void _mtx_lock(mtx_t *mtx, const char *file, int line) {
  if (mtx_get_opts(mtx) & MTX_SPIN) {
    _mtx_spin_lock(mtx, file, line);
  } else {
    _mtx_wait_lock(mtx, file, line);
  }
}

void _mtx_unlock(mtx_t *mtx) {
  if (mtx_get_opts(mtx) & MTX_SPIN) {
    _mtx_spin_unlock(mtx);
  } else {
    _mtx_wait_unlock(mtx);
  }
}

// MARK: mutex lock class interface

void mtx_lc_lock(struct lock_object *lock, uintptr_t how) {
  todo();
}

uintptr_t mtx_lc_unlock(struct lock_object *lock) {
  todo();
}

int mtx_lc_owner(struct lock_object *lock, struct thread **owner) {
  todo();
}
