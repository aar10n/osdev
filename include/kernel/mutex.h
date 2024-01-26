//
// Created by Aaron Gill-Braun on 2023-12-28.
//

#ifndef KERNEL_MUTEX_H
#define KERNEL_MUTEX_H

#include <kernel/lock.h>

// =================================
//              mutex
// =================================

/*
 * Mutal exclusion primitive.
 *
 * A mutex is a synchronization primitive that can be used to protect
 * shared data from being simultaneously accessed by multiple threads.
 */
typedef struct mtx {
  struct lock_object lo;         // common lock state
  volatile uintptr_t mtx_lock;   // mutex owner pointer | mutex state
} mtx_t;

// mutex init options
#define MTX_SPIN      0x1  // spin when blocked (default is context switch)
#define MTX_DEBUG     0x2  // enable debugging for this lock
#define MTX_RECURSIVE 0x4  // allow recursive locking
#define MTX_NOCLAIMS  0x8  // don't track lock claims

// assert options
#define MA_UNLOCKED     LA_UNLOCKED
#define MA_LOCKED       LA_LOCKED
#define MA_OWNED        LA_OWNED
#define MA_NOTOWNED     LA_NOTOWNED
#define MA_RECURSED     LA_RECURSED
#define MA_NOTRECURSED  LA_NOTRECURSED

/* common mutex api */
void _mtx_init(mtx_t *mtx, uint32_t opts, const char *name);
void _mtx_destroy(mtx_t *mtx);
struct thread *_mtx_owner(mtx_t *mtx);
void _mtx_assert(mtx_t *mtx, int what, const char *file, int line);

int _mtx_spin_trylock(mtx_t *mtx, const char *file, int line);
void _mtx_spin_lock(mtx_t *mtx, const char *file, int line);
void _mtx_spin_unlock(mtx_t *mtx);

int _mtx_wait_trylock(mtx_t *mtx, const char *file, int line);
void _mtx_wait_lock(mtx_t *mtx, const char *file, int line);
void _mtx_wait_unlock(mtx_t *mtx);

// This is the actual public api. Only the trylock and lock functions
// need to be macros but we define them all as such for consistency.

#define mtx_init(m,o,n) _mtx_init(m, o, n)
#define mtx_destroy(m) _mtx_destroy(m)
#define mtx_owner(m) _mtx_owner(m)
#define mtx_assert(m, w) _mtx_assert(m, w, __FILE__, __LINE__)

#define mtx_trylock(m) _mtx_wait_trylock(m, __FILE__, __LINE__)
#define mtx_lock(m) _mtx_wait_lock(m, __FILE__, __LINE__)
#define mtx_unlock(m) _mtx_wait_unlock(m)

#define mtx_spin_trylock(m) _mtx_spin_trylock(m, __FILE__, __LINE__)
#define mtx_spin_lock(m) _mtx_spin_lock(m, __FILE__, __LINE__)
#define mtx_spin_unlock(m) _mtx_spin_unlock(m)

void _thread_lock(struct thread *td, const char *file, int line);
void _thread_unlock(struct thread *td);

#endif
