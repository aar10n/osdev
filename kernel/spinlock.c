//
// Created by Aaron Gill-Braun on 2021-03-24.
//

#include <spinlock.h>
#include <atomic.h>
#include <panic.h>
#include <cpu/cpu.h>

extern void preempt_disable();
extern void preempt_enable();

static inline void __preempt_disable() {
  if (PERCPU_THREAD != NULL) {
    preempt_disable();
  } else {
    PERCPU_SET_RFLAGS(cli_save());
  }
}

static inline void __preempt_enable() {
  if (PERCPU_THREAD != NULL) {
    preempt_enable();
  } else {
    sti_restore(PERCPU_RFLAGS);
  }
}

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

  uint8_t id = PERCPU_ID;
  __preempt_disable();
  if (atomic_bit_test_and_set(&lock->locked, 0)) {
    if (lock->locked_by == id) {
      // re-entrant
      lock->lock_count++;
      return;
    }

    __preempt_enable();
    while (atomic_bit_test_and_set(&lock->locked, 0)) {
      cpu_pause(); // spin
    }
    __preempt_disable();
  }
  lock->locked_by = id;
  lock->lock_count = 1;
}

void spin_unlock(spinlock_t *lock) {
  if (lock == NULL) {
    return;
  }

  uint64_t id = PERCPU_ID;
  if (atomic_bit_test_and_set(&lock->locked, 0)) {
    // the lock was set
    kassert(lock->locked_by == id);
    lock->lock_count--;
    if (lock->lock_count == 0) {
      // only clear when last re-entrant lock is released
      atomic_bit_test_and_reset(&lock->locked, 0);
      __preempt_enable();
    }
  } else {
    // spinlock was not locked
  }
}
