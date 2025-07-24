//
// Created by Aaron Gill-Braun on 2023-12-28.
//

#include <kernel/cond.h>
#include <kernel/mutex.h>
#include <kernel/tqueue.h>
#include <kernel/proc.h>
#include <kernel/time.h>

#include <kernel/panic.h>
#include <kernel/printf.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(x, ...) kprintf("cond: " x, ##__VA_ARGS__)
#define EPRINTF(x, ...) kprintf("cond: %s: " x, __func__, ##__VA_ARGS__)


void cond_init(cond_t *cond, const char *name) {
  cond->name = name;
  cond->waiters = 0;
}

void cond_destroy(cond_t *cond) {
  ASSERT(cond->waiters == 0);
  cond->name = NULL;
}

void cond_wait(cond_t *cond, mtx_t *lock) {
  struct waitqueue *waitq = waitq_lookup_or_default(WQ_CONDV, cond, curthread->own_waitq);
  cond->waiters++;

  if (LO_LOCK_CLASS(&lock->lo) == SPINLOCK_LOCKCLASS) {
    mtx_spin_unlock(lock);
  } else {
    mtx_unlock(lock);
  }

  waitq_wait(waitq, cond->name);

  if (LO_LOCK_CLASS(&lock->lo) == SPINLOCK_LOCKCLASS) {
    mtx_spin_lock(lock);
  } else {
    mtx_lock(lock);
  }
}

int cond_wait_timeout(cond_t *cond, mtx_t *lock, struct timespec *ts) {
  uint64_t timeout_ns = timespec_to_nanos(ts);
  struct waitqueue *waitq = waitq_lookup_or_default(WQ_CONDV, cond, curthread->own_waitq);
  cond->waiters++;

  if (LO_LOCK_CLASS(&lock->lo) == SPINLOCK_LOCKCLASS) {
    mtx_spin_unlock(lock);
  } else {
    mtx_unlock(lock);
  }

  int ret = waitq_wait_timeout(waitq, cond->name, timeout_ns);

  if (LO_LOCK_CLASS(&lock->lo) == SPINLOCK_LOCKCLASS) {
    mtx_spin_lock(lock);
  } else {
    mtx_lock(lock);
  }

  cond->waiters--;
  return ret;
}

int cond_wait_sig(cond_t *cond, mtx_t *lock) {
  struct waitqueue *waitq = waitq_lookup_or_default(WQ_CONDV, cond, curthread->own_waitq);
  cond->waiters++;

  if (LO_LOCK_CLASS(&lock->lo) == SPINLOCK_LOCKCLASS) {
    mtx_spin_unlock(lock);
  } else {
    mtx_unlock(lock);
  }

  int ret = waitq_wait_sig(waitq, cond->name);

  if (LO_LOCK_CLASS(&lock->lo) == SPINLOCK_LOCKCLASS) {
    mtx_spin_lock(lock);
  } else {
    mtx_lock(lock);
  }

  cond->waiters--;
  return ret;
}

int cond_wait_sigtimeout(cond_t *cond, mtx_t *lock, struct timespec *ts) {
  uint64_t timeout_ns = timespec_to_nanos(ts);
  struct waitqueue *waitq = waitq_lookup_or_default(WQ_CONDV, cond, curthread->own_waitq);
  cond->waiters++;

  if (LO_LOCK_CLASS(&lock->lo) == SPINLOCK_LOCKCLASS) {
    mtx_spin_unlock(lock);
  } else {
    mtx_unlock(lock);
  }

  int ret = waitq_wait_sigtimeout(waitq, cond->name, timeout_ns);

  if (LO_LOCK_CLASS(&lock->lo) == SPINLOCK_LOCKCLASS) {
    mtx_spin_lock(lock);
  } else {
    mtx_lock(lock);
  }

  cond->waiters--;
  return ret;
}

void cond_signal(cond_t *cond) {
  if (cond->waiters == 0) {
    return;
  }

  struct waitqueue *waitq = waitq_lookup(cond);
  if (waitq != NULL) {
    waitq_signal(waitq);
    cond->waiters--;
  }
}

void cond_broadcast(cond_t *cond) {
  if (cond->waiters == 0) {
    return;
  }

  struct waitqueue *waitq = waitq_lookup(cond);
  if (waitq != NULL) {
    waitq_broadcast(waitq);
    cond->waiters = 0;
  }
}
