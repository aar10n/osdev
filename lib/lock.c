//
// Created by Aaron Gill-Braun on 2020-10-19.
//

#include <cpu/cpu.h>
#include <percpu.h>
#include <atomic.h>
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

//
// Spinlocks
//

void spin_init(spinlock_t *lock) {
  lock->locked = 0;
  lock->locked_by = UINT64_MAX;
  lock->lock_count = 0;
  lock->rflags = 0;
}

void spin_lock(spinlock_t *lock) {
  uint64_t id = PERCPU->id;
  uint64_t rflags = cli_save();
  if (atomic_bit_test_and_set(&lock->locked)) {
    if (lock->locked_by == id) {
      // re-entrant
      lock->lock_count++;
      return;
    }

    sti_restore(rflags);
    while (atomic_bit_test_and_set(&lock->locked)) {
      cpu_pause(); // spin
    }
    cli_save();
  }
  lock->locked_by = id;
  lock->lock_count = 1;
  lock->rflags = rflags;
}

void spin_unlock(spinlock_t *lock) {
  uint64_t id = PERCPU->id;
  uint64_t rflags = lock->rflags;
  if (atomic_bit_test_and_set(&lock->locked)) {
    // the lock was set
    _assert(lock->locked_by == id);
    lock->lock_count--;
    if (lock->lock_count == 0) {
      // only clear when last re-entrant lock is released
      atomic_bit_test_and_reset(&lock->locked);
      sti_restore(rflags);
    }
  } else {
    _assert(false);
  }
}

bool spin_trylock(spinlock_t *lock) {
  uint64_t id = PERCPU->id;
  uint64_t rflags = cli_save();
  if (atomic_bit_test_and_set(&lock->locked)) {
    if (lock->locked_by == id) {
      // re-entrant
      lock->lock_count++;
      return true;
    }

    sti_restore(rflags);
    return false;
  }
  lock->locked_by = id;
  lock->lock_count = 1;
  lock->rflags = rflags;
  return true;
}

//
// Read/Write Spinlock
//

void spinrw_init(rw_spinlock_t *lock) {
  lock->locked = 0;
  lock->rflags = 0;
  lock->reader_count = 0;
}

void spinrw_aquire_read(rw_spinlock_t *lock) {
  if (atomic_bit_test_and_set(&lock->locked)) {
    while (atomic_bit_test_and_set(&lock->locked)) {
      cpu_pause(); // spin
    }
  }
  atomic_fetch_add((uint64_t *) &lock->reader_count, 1);
  atomic_bit_test_and_reset(&lock->locked);
}

void spinrw_release_read(rw_spinlock_t *lock) {
  _assert(lock->reader_count > 0);
  atomic_fetch_add((uint64_t *) &lock->reader_count, -1);
}

void spinrw_aquire_write(rw_spinlock_t *lock) {
  uint64_t rflags = cli_save();
  if (atomic_bit_test_and_set(&lock->locked) || lock->reader_count > 0) {
    sti_restore(rflags);
    while (atomic_bit_test_and_set(&lock->locked) || lock->reader_count > 0) {
      cpu_pause(); // spin
    }
    cli_save();
  }
  lock->rflags = rflags;
}

void spinrw_release_write(rw_spinlock_t *lock) {
  _assert(lock->locked);
  uint64_t rflags = lock->rflags;
  if (atomic_bit_test_and_set(&lock->locked)) {
    atomic_bit_test_and_reset(&lock->locked);
    sti_restore(rflags);
  } else {
    _assert(false);
  }
}
