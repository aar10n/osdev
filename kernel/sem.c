//
// Created by Aaron Gill-Braun on 2024-11-13.
//

#include <kernel/sem.h>
#include <kernel/proc.h>
#include <kernel/panic.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(x, ...) kprintf("sem: " x, ##__VA_ARGS__)


static inline void acquire_sem_lock(sem_t *sem) {
  if (LO_LOCK_CLASS(&sem->lock.lo) == SPINLOCK_LOCKCLASS) {
    mtx_spin_lock(&sem->lock);
  } else {
    mtx_lock(&sem->lock);
  }
}

static inline void release_sem_lock(sem_t *sem) {
  if (LO_LOCK_CLASS(&sem->lock.lo) == SPINLOCK_LOCKCLASS) {
    mtx_spin_unlock(&sem->lock);
  } else {
    mtx_unlock(&sem->lock);
  }
}

//
// MARK: Semaphore API
//

void _sem_init(sem_t *sem, int value, uint32_t opts, const char *name) {
  uint32_t mtx_opts = MTX_RECURSIVE;
  mtx_opts |= (opts & SEM_SPIN) ? MTX_SPIN : 0;
  mtx_opts |= (opts & SEM_DEBUG) ? MTX_DEBUG : 0;
  mtx_init(&sem->lock, mtx_opts, name);
  atomic_store(&sem->count, value);
}

void _sem_destroy(sem_t *sem) {
  mtx_destroy(&sem->lock);
}

int _sem_try_down(sem_t *sem, const char *file, int line) {
  int count = atomic_load(&sem->count);
  while (count > 0) {
    if (atomic_cmpxchg(&sem->count, count, count - 1)) {
      return 1;
    }
    count = atomic_load(&sem->count);
  }
  return 0;
}

void _sem_down(sem_t *sem, const char *file, int line) {
  // fast path: try atomic decrement first
  int count = atomic_load(&sem->count);
  while (count > 0) {
    if (atomic_cmpxchg(&sem->count, count, count - 1)) {
      return;
    }
    count = atomic_load(&sem->count);
  }

  // slow path: acquire the semaphore lock
  acquire_sem_lock(sem);

  // check count again after acquiring lock
  count = atomic_load(&sem->count);
  if (count > 0) {
    atomic_fetch_sub(&sem->count, 1);
    release_sem_lock(sem);
    return;
  }

  // count is 0, add to waitqueue
  struct waitqueue *waitq = waitq_lookup_or_default(WQ_SEMA, sem, curthread->own_waitq);
  release_sem_lock(sem);  // release sem lock before adding to waitqueue
  waitq_wait(waitq, "semaphore down"); // block thread on waitqueue
}

void _sem_up(sem_t *sem, const char *file, int line) {
  acquire_sem_lock(sem);

  // wake up one waiter if any
  struct waitqueue *waitq = waitq_lookup(sem);
  if (waitq) {
    waitq_signal(waitq);
  } else {
    // no waiters, increment the count
    atomic_fetch_add(&sem->count, 1);
  }

  release_sem_lock(sem);
}
