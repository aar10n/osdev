//
// Created by Aaron Gill-Braun on 2020-10-19.
//

#include <cpu/cpu.h>
#include <percpu.h>
#include "lock.h"

#ifndef _assert
#include <panic.h>
#define _assert(expr) kassert((expr))
#endif

#ifndef _malloc
#include <mm/heap.h>
#define _malloc(size) kmalloc(size)
#define _free(ptr) kfree(ptr)
#endif


spinlock_t *spin_create() {
  spinlock_t *lock = _malloc(sizeof(spinlock_t));
  atomic_flag_clear(&lock->locked);
  spin_init(lock);
  return lock;
}

void spin_destroy(spinlock_t *lock) {
  bool value = atomic_flag_test_and_set(&lock->locked);
  _assert(value == false && lock->lock_count == 0);
  _free(lock);
}

void spin_init(spinlock_t *lock) {
  atomic_flag_clear(&lock->locked);
  lock->rflags = 0;
  lock->locked_by = UINT64_MAX;
  lock->lock_count = 0;
}

void spin_lock(spinlock_t *lock) {
  uint64_t id = PERCPU->id;
  uint64_t rflags = cli_save();
  if (atomic_flag_test_and_set(&lock->locked)) {
    if (lock->locked_by == id) {
      // re-entrant
      lock->lock_count++;
      return;
    }

    sti_restore(rflags);
    while (atomic_flag_test_and_set(&lock->locked)) {
      cpu_pause(); // spin
    }
    cli_save();
  }
  lock->rflags = rflags;
  lock->locked_by = id;
  lock->lock_count = 1;
}

void spin_unlock(spinlock_t *lock) {
  uint64_t id = PERCPU->id;
  uint64_t rflags = lock->rflags;
  if (atomic_flag_test_and_set(&lock->locked)) {
    // the lock was set
    _assert(lock->locked_by == id);
    lock->lock_count--;
    if (lock->lock_count == 0) {
      // only clear when last re-entrant lock is released
      atomic_flag_clear(&lock->locked);
      sti_restore(rflags);
    }
  } else {
    // we shouldn't be trying to unlock this
    panic("[CPU %d] attemping to unlock a free spinlock", id);
  }
}

bool spin_trylock(spinlock_t *lock) {
  uint64_t id = PERCPU->id;
  uint64_t rflags = cli_save();
  if (atomic_flag_test_and_set(&lock->locked)) {
    if (lock->locked_by == id) {
      // re-entrant
      lock->lock_count++;
      return true;
    }

    sti_restore(rflags);
    return false;
  }
  lock->rflags = rflags;
  lock->locked_by = id;
  lock->lock_count = 1;
  return true;
}
