//
// Created by Aaron Gill-Braun on 2021-03-24.
//

#ifndef KERNEL_SPINLOCK_H
#define KERNEL_SPINLOCK_H

#include <base.h>

typedef struct spinlock {
  volatile uint8_t locked;
  uint8_t locked_by;
  uint16_t lock_count;
} spinlock_t;

void spin_init(spinlock_t *lock);
int spin_lock(spinlock_t *lock);
int spin_trylock(spinlock_t *lock);
int spin_unlock(spinlock_t *lock);

#define SPIN_LOCK(lock) __type_checked(spinlock_t *, lock, spin_lock(lock))
#define SPIN_UNLOCK(lock) __type_checked(spinlock_t *, lock, spin_unlock(lock))

#endif
