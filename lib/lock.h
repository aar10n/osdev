//
// Created by Aaron Gill-Braun on 2020-10-19.
//

#ifndef LIB_LOCK_H
#define LIB_LOCK_H

#include <base.h>
#include <atomic.h>

#define lock(L) spin_lock(&L)
#define unlock(L) spin_unlock(&L)

// rentrant spinlock
typedef struct {
  volatile uint8_t locked;
  uint64_t locked_by;
  uint64_t lock_count;
  uint64_t rflags;
} spinlock_t;

void spin_init(spinlock_t *lock);
void spin_lock(spinlock_t *lock);
void spin_unlock(spinlock_t *lock);
bool spin_trylock(spinlock_t *lock);

// read/write spinlock
typedef struct {
  volatile uint8_t locked;
  volatile uint64_t reader_count;
  uint64_t rflags;
} rw_spinlock_t;

void spinrw_init(rw_spinlock_t *lock);
void spinrw_aquire_read(rw_spinlock_t *lock);
void spinrw_release_read(rw_spinlock_t *lock);
void spinrw_aquire_write(rw_spinlock_t *lock);
void spinrw_release_write(rw_spinlock_t *lock);

#endif
