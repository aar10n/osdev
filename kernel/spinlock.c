//
// Created by Aaron Gill-Braun on 2021-03-24.
//

#include <spinlock.h>
#include <atomic.h>
#include <panic.h>
#include <thread.h>
#include <clock.h>

#include <cpu/cpu.h>

//

void spin_init(spinlock_t *lock) {
  lock->locked = 0;
  lock->locked_by = 0;
  lock->lock_count = 0;
}

void spin_lock(spinlock_t *lock) {
  if (lock == NULL) {
    return;
  }

  uint64_t id = PERCPU_ID;
  uint64_t rflags = cpu_save_clear_interrupts(); // disable interrupts
  if (atomic_lock_test_and_set(&lock->locked)) {
    // lock is currently held
    if (lock->locked_by == id) {
      // held by us so it's re-entrant
      lock->lock_count++;
      return;
    }

    // wait for spinlock
    register uint64_t timeout asm ("r15") = 10000000 + (PERCPU_ID * 1000000);

    cpu_enable_interrupts(); // enable interrupts
    while (atomic_lock_test_and_set(&lock->locked)) {
      cpu_pause(); // spin
      timeout--;
      if (timeout == 0) {
        panic("stuck waiting for spinlock %p [locked = %d, held by %u, lock_count = %d]",
              lock, lock->locked, lock->locked_by, lock->lock_count);
      }
    }
    cpu_disable_interrupts(); // re-disable interrupts
  }

  // interrupts are disabled while the lock is held
  lock->locked_by = id;
  lock->lock_count = 1;
  PERCPU_SET_RFLAGS(rflags);
}

int spin_trylock(spinlock_t *lock) {
  if (lock == NULL) {
    return 0;
  }

  uint64_t id = PERCPU_ID;
  uint64_t rflags = cpu_save_clear_interrupts(); // disable interrupts
  if (atomic_lock_test_and_set(&lock->locked)) {
    // lock is currently held
    if (lock->locked_by == id) {
      // held by us so it's re-entrant
      lock->lock_count++;
      return 1;
    }

    // the lock is already claimed
    cpu_restore_interrupts(rflags); // restore interrupts
    return 0;
  }

  // interrupts are disabled while the lock is held
  PERCPU_SET_RFLAGS(rflags);
  lock->locked_by = id;
  lock->lock_count = 1;
  return 1;
}

void spin_unlock(spinlock_t *lock) {
  if (lock == NULL) {
    return;
  }

  uint64_t id = PERCPU_ID;
  if (atomic_lock_test_and_set(&lock->locked)) {
    kassert(lock->locked_by == id);
    if (lock->lock_count == 1) {
      // only clear when last re-entrant lock is released
      atomic_lock_test_and_reset(&lock->locked);
      lock->lock_count = 0;
      cpu_restore_interrupts(PERCPU_RFLAGS); // restore interrupts
      return;
    }

    // re-entrant unlock
    lock->lock_count--;
    return;
  }

  // lock was not held by anyone
  panic("spin_unlock() on lock that is not held [%p]", lock);
}
