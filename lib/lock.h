//
// Created by Aaron Gill-Braun on 2020-10-19.
//

#ifndef LIB_LOCK_H
#define LIB_LOCK_H

#include <base.h>
#include <stdatomic.h>

// rentrant spinlock
typedef struct {
  volatile atomic_flag locked;
  uint64_t rflags;
  uint64_t locked_by;
  uint64_t lock_count;
} spinlock_t;

// #define

void spin_init(spinlock_t *lock);
void spin_lock(spinlock_t *lock);
void spin_unlock(spinlock_t *lock);
bool spin_trylock(spinlock_t *lock);

#endif
