//
// Created by Aaron Gill-Braun on 2023-12-28.
//

#ifndef KERNEL_RWLOCK_H
#define KERNEL_RWLOCK_H

#include <kernel/lock.h>

// =================================
//              rwlock
// =================================

/*
 * Read-write lock primitive.
 *
 * A read-write lock allows multiple readers or a single writer to
 * access a resource. It is used to protect data structures that are
 * read often but modified infrequently.
 */
typedef struct rwlock {
  struct lock_object lo;          // common lock state
  volatile uintptr_t mtx_lock;    // mutex lock
  volatile uint32_t readers;      // number of readers
} rwlock_t;

// rwlock options
#define RW_DEBUG     0x1  // enable debugging for this lock
#define RW_RECURSE   0x2  // allow recursive locking
#define RW_NOCLAIM   0x4  // don't track lock claims
#define RW_OPT_MASK  0xF

// assert options
#define RWA_UNLOCKED LA_UNLOCKED
#define RWA_LOCKED   LA_LOCKED
#define RWA_SLOCKED  LA_SLOCKED
#define RWA_XLOCKED  LA_XLOCKED
#define RWA_RECURSED LA_RECURSED
#define RWA_OWNED    100 // lock is owned
#define RWA_NOTOWNED 101 // lock is not owned

/* common mutex api */
void _rw_init(rwlock_t *rw, uint32_t opts, const char *name);
void _rw_destroy(rwlock_t *rw);
void _rw_assert(rwlock_t *rw, int what, const char *file, int line);

int _rw_try_rlock(rwlock_t *rw, const char *file, int line);
int _rw_try_wlock(rwlock_t *rw, const char *file, int line);
void _rw_rlock(rwlock_t *rw, const char *file, int line);
void _rw_wlock(rwlock_t *rw, const char *file, int line);
void _rw_runlock(rwlock_t *rw);
void _rw_wunlock(rwlock_t *rw);

// public api

#define rw_init(rw, o, n) _rw_init(rw, o, n)
#define rw_destroy(rw) _rw_destroy(rw)
#define rw_locked(rw) _rw_locked(rw)
#define rw_owned(rw) _rw_owned(rw)
#define rw_assert(rw, w, f, l) _rw_assert(rw, w, f, l)

#define rw_try_rlock(rw) _rw_try_rlock(rw, __FILE__, __LINE__)
#define rw_try_wlock(rw) _rw_try_wlock(rw, __FILE__, __LINE__)
#define rw_rlock(rw) _rw_rlock(rw, __FILE__, __LINE__)
#define rw_wlock(rw) _rw_wlock(rw, __FILE__, __LINE__)
#define rw_runlock(rw) _rw_runlock(rw)
#define rw_wunlock(rw) _rw_wunlock(rw)

#endif
