//
// Created by Aaron Gill-Braun on 2023-12-28.
//

#include <kernel/mutex.h>
#include <kernel/proc.h>
#include <kernel/tqueue.h>
#include <kernel/panic.h>
#include <kernel/printf.h>

#define ASSERT(x, fmt, ...) kassertf(x, fmt, __VA_ARGS__)

#define new_mtx_lock(td, state) ((uintptr_t)(td) | ((state) & MTX_STATE_MASK))
#define mtx_lock_owner(ml) ((thread_t *) ((ml) & ~MTX_STATE_MASK))
#define mtx_get_owner(m) mtx_lock_owner((m)->mtx_lock)
#define mtx_get_lc(m) LO_LOCK_CLASS(&(m)->lo)
#define mtx_get_lo(m) LO_LOCK_OPTS(&(m)->lo)

// mutex state
#define MTX_UNOWNED     0x00 // free mutex state
#define MTX_LOCKED      0x01 // mutex is locked
#define MTX_DESTROYED   0x02 // destroyed mutex state
#define MTX_RECURSED    0x04 // mutex is locked recursively (non-spin)
#define MTX_STATE_MASK  0x07

#define MTX_DEBUGF(m, file, line, fmt, ...) \
  { if (__expect_false((m)->lo.flags & LO_DEBUG)) kprintf("mtx: " fmt " [%s:%d]\n", ##__VA_ARGS__, file, line); }

#define SPIN_CLAIMS_ADD(lock_obj, file, line) if (__expect_true(curcpu_spin_claims != NULL)) \
    lock_claim_list_add(curcpu_spin_claims, lock_obj, 0, file, line)
//#define SPIN_CLAIMS_ADD(lock_obj, file, line)
#define SPIN_CLAIMS_REMOVE(lock_obj) if (__expect_true(curcpu_spin_claims != NULL)) \
    lock_claim_list_remove(curcpu_spin_claims, lock_obj)
//#define SPIN_CLAIMS_REMOVE(lock_obj)

// #define WAIT_CLAIMS_ADD(lock_obj, file, line) if (__expect_true(curthread != NULL)) \
//     lock_claim_list_add(curthread->wait_claims, lock_obj, 0, file, line)
#define WAIT_CLAIMS_ADD(lock_obj, file, line)
// #define WAIT_CLAIMS_REMOVE(lock_obj) if (__expect_true(curthread != NULL)) \
//     lock_claim_list_remove(curthread->wait_claims, lock_obj)
#define WAIT_CLAIMS_REMOVE(lock_obj)

static struct lock_class spinlock_lockclass = {
  .name = "spinlock",
  .flags = SPINLOCK_LOCKCLASS,
  .lc_lock = mtx_lockclass_lock,
  .lc_unlock = mtx_lockclass_unlock,
  .lc_assert = mtx_lockclass_assert,
  .lc_owner = mtx_lockclass_owner,
};

static struct lock_class mutex_lockclass = {
  .name = "mutex",
  .flags = MUTEX_LOCKCLASS,
  .lc_lock = mtx_lockclass_lock,
  .lc_unlock = mtx_lockclass_unlock,
  .lc_assert = mtx_lockclass_assert,
  .lc_owner = mtx_lockclass_owner,
};

static inline void spinlock_enter() {
  if (__expect_true(curthread != NULL)) {
    atomic_fetch_add(&curthread->spin_count, 1);
  }
  critical_enter();
}

static inline void spinlock_exit() {
  if (__expect_true(curthread != NULL)) {
    ASSERT(curthread->spin_count > 0, "spinlock_exit() with no spin locks held");
    atomic_fetch_sub(&curthread->spin_count, 1);
  }
  critical_exit();
}

//

static void mtx_static_init() {
  lock_classes[lock_class_index(SPINLOCK_LOCKCLASS)] = &spinlock_lockclass;
  lock_classes[lock_class_index(MUTEX_LOCKCLASS)] = &mutex_lockclass;
}
STATIC_INIT(mtx_static_init);

uint32_t _mtx_opts_to_lockobject_flags(uint32_t opts) {
  uint32_t flags = LO_INITIALIZED;
  if (opts & MTX_SPIN) {
    flags |= SPINLOCK_LOCKCLASS;
    opts &= ~MTX_RECURSIVE; // not allowed
  } else {
    flags |= MUTEX_LOCKCLASS;
  }

  flags |= opts & MTX_DEBUG ? LO_DEBUG : 0;
  flags |= opts & MTX_NOCLAIMS ? LO_NOCLAIMS : 0;
  flags |= opts & MTX_RECURSIVE ? LO_RECURSABLE : 0;
  return flags;
}

//
// MARK: Public Mutex API
//

void _mtx_init(mtx_t *mtx, uint32_t opts, const char *name) {
  mtx->lo.name = name;
  mtx->lo.flags = _mtx_opts_to_lockobject_flags(opts);
  mtx->lo.data = 0; // recurse count (non-spin)
  mtx->mtx_lock = MTX_UNOWNED;
}

void _mtx_destroy(mtx_t *mtx) {
  MTX_DEBUGF(mtx, __FILE__, __LINE__, "destroy {:#Lo}", mtx);
  ASSERT(mtx->mtx_lock != MTX_DESTROYED, "_mtx_destroy() on locked mutex");
  mtx->lo.flags = MTX_DESTROYED;
  mtx->lo.data = 0;
  mtx->mtx_lock = MTX_DESTROYED;
}

void _mtx_assert(mtx_t *mtx, int what, const char *file, int line) {
  uintptr_t mtx_lock = mtx->mtx_lock;
  thread_t *owner = mtx_lock_owner(mtx_lock);
  uint8_t state = mtx_lock & MTX_STATE_MASK;
  if (what == MA_UNLOCKED) {
    kassertf(mtx_lock == MTX_UNOWNED, "mutex locked, %s:%d", file, line);
  } else if (what & MA_LOCKED) {
    kassertf(mtx_lock & MTX_LOCKED, "mutex unlocked, %s:%d", file, line);
  } else if (what & MA_OWNED) {
    kassertf(mtx_lock & MTX_LOCKED && owner == curthread, "mutex not owned, [owner = {:td}] %s:%d", owner, file, line);
  } else if (what & MA_NOTOWNED) {
    kassertf(owner != curthread, "mutex owned, %s:%d", file, line);
  } else if (what & MA_RECURSED) {
    kassertf(mtx_lock & MTX_LOCKED && mtx->lo.data > 0, "mutex not recursed, %s:%d", file, line);
  } else if (what & MA_NOTRECURSED) {
    kassertf(mtx_lock & MTX_LOCKED && mtx->lo.data == 1, "mutex recursed, %s:%d", file, line);
  } else {
    panic("invalid mutex assertion");
  }
}

thread_t *_mtx_owner(mtx_t *mtx) {
  uintptr_t mtx_lock = mtx->mtx_lock;
  ASSERT(mtx_lock != MTX_DESTROYED, "_mtx_destroy() on locked mutex");
  return mtx_lock_owner(mtx_lock);
}

struct lock_class *_mtx_get_lockclass(mtx_t *mtx) {
  ASSERT(mtx->mtx_lock != MTX_DESTROYED, "_mtx_get_lockclass() on destroyed mutex");
  if (mtx_get_lc(mtx) == SPINLOCK_LOCKCLASS) {
    return &spinlock_lockclass;
  } else if (mtx_get_lc(mtx) == MUTEX_LOCKCLASS) {
    return &mutex_lockclass;
  }
  panic("unknown lock class for mutex %p", mtx);
}

/////////////////////////
// mtx_spin_lock

int _mtx_spin_trylock(mtx_t *mtx, const char *file, int line) {
  thread_t *owner = mtx_get_owner(mtx);
  ASSERT(mtx->mtx_lock != MTX_DESTROYED, "_mtx_spin_trylock() on destroyed mutex, %s:%d", file, line);
  ASSERT(mtx_get_lc(mtx) == SPINLOCK_LOCKCLASS, "_mtx_spin_trylock() on non-spin mutex, %s:%d", file, line);
  ASSERT(owner == NULL || owner != curthread, "_mtx_spin_trylock() on owned mutex, %s:%d", file, line);

  spinlock_enter();
  SPIN_CLAIMS_ADD(&mtx->lo, file, line);

  if (curthread != NULL && mtx_get_owner(mtx) == curthread) {
    ASSERT(mtx_get_lo(mtx) & LO_RECURSABLE, "_mtx_wait_lock() on non-recursive mutex, %s:%d", file, line);
    // recursed lock
    mtx->mtx_lock |= MTX_RECURSED;
    mtx->lo.data++;
    curthread->lock_count++;
    return 1;
  }

  uintptr_t mtx_lock = new_mtx_lock(curthread, MTX_LOCKED);
  if (atomic_cmpxchg_acq(&mtx->mtx_lock, MTX_UNOWNED, mtx_lock)) {
    mtx->lo.data = 1;
    return 1;
  }

  SPIN_CLAIMS_REMOVE(&mtx->lo);
  spinlock_exit();
  return 0;
}

void _mtx_spin_lock(mtx_t *mtx, const char *file, int line) {
  thread_t *owner = mtx_get_owner(mtx);
  int lc = mtx_get_lc(mtx);
  ASSERT(mtx->mtx_lock != MTX_DESTROYED, "_mtx_spin_lock() on destroyed mutex [%p] %s:%d", mtx, file, line);
  ASSERT(mtx_get_lc(mtx) == SPINLOCK_LOCKCLASS, "_mtx_spin_lock() on non-spin mutex [%p] %s:%d", mtx, file, line);

  spinlock_enter();
  SPIN_CLAIMS_ADD(&mtx->lo, file, line);

  if (curthread != NULL && mtx_get_owner(mtx) == curthread) {
    ASSERT(mtx_get_lo(mtx) & LO_RECURSABLE, "_mtx_wait_lock() on non-recursive mutex, %s:%d", file, line);
    // recursed lock
    mtx->mtx_lock |= MTX_RECURSED;
    mtx->lo.data++;
    curthread->lock_count++;
    return;
  }

  struct spin_delay delay = new_spin_delay(SHORT_DELAY, MAX_RETRIES);
  uintptr_t mtx_lock = new_mtx_lock(curthread, MTX_LOCKED);
  for (;;) {
    // https://rigtorp.se/spinlock/
    // test and test-and-set lock optimize for uncontended case
    if (atomic_cmpxchg_acq(&mtx->mtx_lock, MTX_UNOWNED, mtx_lock)) {
      break;
    }
    while (atomic_load_relaxed(&mtx->mtx_lock) != MTX_UNOWNED) {
      if (!spin_delay_wait(&delay)) {
        // possible deadlock?
        panic("spin mutex deadlock, %s:%d", file, line);
      }
    }
  }
  mtx->lo.data = 1;
}

void _mtx_spin_unlock(mtx_t *mtx, const char *file, int line) {
  thread_t *owner = mtx_get_owner(mtx);
  thread_t *current = curthread;
  ASSERT(mtx->mtx_lock != MTX_DESTROYED, "_mtx_spin_unlock() on destroyed mutex");
  ASSERT(mtx_get_lc(mtx) == SPINLOCK_LOCKCLASS, "_mtx_spin_unlock() on non-spin mutex");
  ASSERT(owner == curthread, "_mtx_spin_unlock() on unowned mutex");

  mtx->lo.data--;
  if (mtx->mtx_lock & MTX_RECURSED && mtx->lo.data > 0) {
    ASSERT(mtx_get_lo(mtx) & LO_RECURSABLE, "_mtx_spin_unlock() on non-recursive mutex");
    if (mtx->lo.data == 1) {
      mtx->mtx_lock &= ~MTX_RECURSED;
    }
    return;
  }

  ASSERT(mtx->lo.data == 0, "_mtx_spin_unlock() expected 0 count, got %d", mtx->lo.data);
  atomic_store_release(&mtx->mtx_lock, MTX_UNOWNED);

  SPIN_CLAIMS_REMOVE(&mtx->lo);
  spinlock_exit();
}

/////////////////////////
// mtx_wait_lock

int _mtx_wait_trylock(mtx_t *mtx, const char *file, int line) {
  MTX_DEBUGF(mtx, file, line, "wait_trylock {:#Lo} curthread={:td}", mtx, curthread);
  ASSERT(mtx->mtx_lock != MTX_DESTROYED, "_mtx_wait_trylock() on destroyed mutex, %s:%d", file, line);
  ASSERT(mtx_get_lc(mtx) == MUTEX_LOCKCLASS, "_mtx_wait_trylock() on non-wait mutex, %s:%d", file, line);

  WAIT_CLAIMS_ADD(&mtx->lo, file, line);

  if (curthread != NULL && mtx_get_owner(mtx) == curthread) {
    ASSERT(mtx_get_lo(mtx) & LO_RECURSABLE, "_mtx_wait_trylock() on non-recursive mutex, %s:%d", file, line);
    mtx->mtx_lock |= MTX_RECURSED;
    mtx->lo.data++;
    curthread->lock_count++;
    return 1;
  }

  uintptr_t mtx_lock = new_mtx_lock(curthread, MTX_LOCKED);
  if (atomic_cmpxchg_acq(&mtx->mtx_lock, MTX_UNOWNED, mtx_lock)) {
    // uncontended lock
    mtx->lo.data = 1;
    curthread->lock_count++;
    return 1;
  }

  WAIT_CLAIMS_REMOVE(&mtx->lo);
  return 0;
}

void _mtx_wait_lock(mtx_t *mtx, const char *file, int line) {
  MTX_DEBUGF(mtx, file, line, "wait_lock {:#Lo} lock={:#x} owner={:td} curthread={:td}", mtx, mtx->mtx_lock, mtx_lock_owner(mtx->mtx_lock), curthread);
  ASSERT(mtx->mtx_lock != MTX_DESTROYED, "_mtx_wait_lock() on destroyed mutex, %s:%d", file, line);
  ASSERT(mtx_get_lc(mtx) == MUTEX_LOCKCLASS, "_mtx_wait_lock() on non-wait mutex, %s:%d", file, line);

  WAIT_CLAIMS_ADD(&mtx->lo, file, line);

  thread_t *td = curthread;
  if (td != NULL && mtx_get_owner(mtx) == td) {
    MTX_DEBUGF(mtx, file, line, "wait_lock {:#Lo} recursed", mtx);
    ASSERT(mtx_get_lo(mtx) & LO_RECURSABLE, "_mtx_wait_lock() on non-recursive mutex, %s:%d", file, line);
    // recursed lock
    mtx->mtx_lock |= MTX_RECURSED;
    mtx->lo.data++;
    td->lock_count++;
    return;
  }

  for (;;) {
    uintptr_t mtx_lock = new_mtx_lock(td, MTX_LOCKED);
    if (atomic_cmpxchg_acq(&mtx->mtx_lock, MTX_UNOWNED, mtx_lock)) {
      // lock claimed
      mtx->lo.data = 1;
      td->lock_count++;
      return;
    }

    // lock is contended - wait for it
    struct thread *owner = mtx_get_owner(mtx);
    struct lockqueue *lockq = lockq_lookup_or_default(&mtx->lo, td->own_lockq);
    lockq_wait(lockq, owner, LQ_EXCL);
    // try to reacquire the lock again
  }
}

void _mtx_wait_unlock(mtx_t *mtx, const char *file, int line) {
  thread_t *owner = mtx_lock_owner(mtx->mtx_lock);
  MTX_DEBUGF(mtx, file, line, "wait_unlock {:#Lo} lock={:#x} owner={:td} curthread={:td}", mtx, mtx->mtx_lock, owner, curthread);
  if (owner == NULL) {
    panic("!!! wait_unlock on unowned mutex\n");
  }

  ASSERT(mtx->mtx_lock != MTX_DESTROYED, "_mtx_wait_unlock() on destroyed mutex");
  ASSERT(mtx_get_lc(mtx) == MUTEX_LOCKCLASS, "_mtx_wait_unlock() on non-wait mutex");
  ASSERT(owner == curthread, "_mtx_wait_unlock() by {:td} on mutex owned by {:td}", curthread, owner);

  mtx->lo.data--;
  curthread->lock_count--;
  if (mtx->mtx_lock & MTX_RECURSED && mtx->lo.data > 0) {
    MTX_DEBUGF(mtx, file, line, "wait_unlock {:#Lo} recursed", mtx);
    ASSERT(mtx_get_lo(mtx) & LO_RECURSABLE, "_mtx_wait_unlock() on non-recursive mutex");
    if (mtx->lo.data == 1) {
      mtx->mtx_lock &= ~MTX_RECURSED;
    }
    MTX_DEBUGF(mtx, file, line, "--> lock={:#x}", mtx->mtx_lock);
    return;
  }

  ASSERT(mtx->lo.data == 0, "_mtx_wait_unlock() expected 0 count, got %d", mtx->lo.data);
  atomic_store_release(&mtx->mtx_lock, MTX_UNOWNED);

  WAIT_CLAIMS_REMOVE(&mtx->lo);
}

//
// MARK: Lock Object API
//

void mtx_lockclass_lock(struct lock_object *lo, uintptr_t how, const char *file, int line) {
  ASSERT(LO_LOCK_CLASS(lo) == LC_SPINLOCK || LO_LOCK_CLASS(lo) == LC_WAITLOCK,
         "mtx_lockclass_lock() called on invalid lock class %s, expected spinlock or waitlock",
         lock_class_kind_str(LO_LOCK_CLASS(lo)));
  ASSERT(how == LC_EXCL, "mtx_lockclass_lock() called with invalid 'how' %d, expected LC_EXCL", how);

  mtx_t *mtx = (mtx_t *) lo;
  if (mtx_get_lo(mtx) & MTX_SPIN) {
    _mtx_spin_lock(mtx, file, line);
  } else {
    _mtx_wait_lock(mtx, file, line);
  }
}

void mtx_lockclass_unlock(struct lock_object *lo, const char *file, int line) {
  ASSERT(LO_LOCK_CLASS(lo) == LC_SPINLOCK || LO_LOCK_CLASS(lo) == LC_WAITLOCK,
         "mtx_lockclass_unlock() called on invalid lock class %s, expected spinlock or waitlock",
         lock_class_kind_str(LO_LOCK_CLASS(lo)));

  mtx_t *mtx = (mtx_t *) lo;
  if (mtx_get_lo(mtx) & MTX_SPIN) {
    _mtx_spin_unlock(mtx, file, line);
  } else {
    _mtx_wait_unlock(mtx, file, line);
  }
}

void mtx_lockclass_assert(struct lock_object *lo, int what, const char *file, int line) {
  ASSERT(LO_LOCK_CLASS(lo) == LC_SPINLOCK || LO_LOCK_CLASS(lo) == LC_WAITLOCK,
         "mtx_lockclass_assert() called on invalid lock class %s, expected spinlock or waitlock",
         lock_class_kind_str(LO_LOCK_CLASS(lo)));

  mtx_t *mtx = (mtx_t *) lo;
  _mtx_assert(mtx, what, file, line);
}

struct thread *mtx_lockclass_owner(struct lock_object *lo) {
  ASSERT(LO_LOCK_CLASS(lo) == LC_SPINLOCK || LO_LOCK_CLASS(lo) == LC_WAITLOCK,
         "mtx_lockclass_owner() called on invalid lock class %s, expected spinlock or waitlock",
         lock_class_kind_str(LO_LOCK_CLASS(lo)));

  mtx_t *mtx = (mtx_t *) lo;
  return _mtx_owner(mtx);
}

ASSERT_IS_TYPE(lockclass_lock_t, mtx_lockclass_lock);
ASSERT_IS_TYPE(lockclass_unlock_t, mtx_lockclass_unlock);
ASSERT_IS_TYPE(lockclass_assert_t, mtx_lockclass_assert);
ASSERT_IS_TYPE(lockclass_owner_t , mtx_lockclass_owner);

//

void _thread_lock(thread_t *td, const char *file, int line) {
  _mtx_spin_lock(&td->lock, file, line);
}

void _thread_unlock(thread_t *td, const char *file, int line) {
  __assert_stack_is_aligned();
  _mtx_spin_unlock(&td->lock, file, line);
}
