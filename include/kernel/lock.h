//
// Created by Aaron Gill-Braun on 2023-12-22.
//

#ifndef KERNEL_LOCK_H
#define KERNEL_LOCK_H

#include <kernel/base.h>
#include <kernel/queue.h>

// https://docs.freebsd.org/en/books/arch-handbook/smp/
// https://man.freebsd.org/cgi/man.cgi?locking(9)
// https://man.freebsd.org/cgi/man.cgi?query=mutex&sektion=9&apropos=0&manpath=FreeBSD+14.0-RELEASE+and+Ports
// https://cgit.freebsd.org/src/tree/sys/sys/lock.h

struct thread;
struct mtx;

struct lock_object;
struct lock_class;

struct lock_claim;
struct lock_claim_list;

/*
 * An implementation of a lock class.
 */
struct lock_class {
  const char *lc_name;
  int (*lc_owner)(struct lock_object *lock, struct thread **owner);
  void (*lc_lock)(struct lock_object *lock, uintptr_t how);
  uintptr_t (*lc_unlock)(struct lock_object *lock);
};

/*
 * Common lock object.
 *
 * All lock objects embed the lock_object at their start.
 */
struct lock_object {
#define embed_lock_object \
  const char *name;         /* lock name */ \
  uint32_t state;           /* lock state */ \
  uint32_t data;            /* class-specific data */
  embed_lock_object
};

struct lock_claim_list *lock_claim_list_alloc();
void lock_claim_list_free(struct lock_claim_list **listp);
void lock_claim_list_add(struct lock_claim_list *list, struct lock_object *lock, uintptr_t how, const char *file, int line);
void lock_claim_list_remove(struct lock_claim_list *list, struct lock_object *lock);

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
  struct lock_object lo;         // common lock object
  volatile uintptr_t owner_opts; // owner thread ptr (8-byte aligned) bitwise
                                 // or'd with the mutex options
} mtx_t;

// mutex options
#define MTX_DEBUG     0x1  // enable debugging for this lock
#define MTX_SPIN      0x2  // spin when blocked (default is context switch)
#define MTX_RECURSE   0x4  // allow recursive locking
#define MTX_OPT_MASK  0x7

int _mtx_spin_trylock(mtx_t *mtx, const char *file, int line);
void _mtx_spin_lock(mtx_t *mtx, const char *file, int line);
void _mtx_spin_unlock(mtx_t *mtx);

int _mtx_wait_trylock(mtx_t *mtx, const char *file, int line);
void _mtx_wait_lock(mtx_t *mtx, const char *file, int line);
void _mtx_wait_unlock(mtx_t *mtx);

/* common mutex api */
void _mtx_init(mtx_t *mtx, uint32_t opts, const char *name);
void _mtx_destroy(mtx_t *mtx);
int _mtx_locked(mtx_t *mtx);
int _mtx_owned(mtx_t *mtx);

int _mtx_assert(mtx_t *mtx, int what, const char *file, int line);
int _mtx_trylock(mtx_t *mtx, const char *file, int line);
void _mtx_lock(mtx_t *mtx, const char *file, int line);
void _mtx_unlock(mtx_t *mtx);

// assert options
#define MA_OWNED    0x1 // lock is owned
#define MA_NOTOWNED 0x2 // lock is not owned
#define MA_RECURSED 0x4 // lock is recursed


// This is the actual public api. Only the trylock and lock functions
// need to be macros but we define them all as such for consistency.

#define MTX_INITIALIZER(opts) { \
  .name = __FILE__ ":" __macro_str(__LINE__), \
  .state = 0, \
  .data = 0, \
  .owner_opts = ((opts) & MTX_OPT_MASK) \
}

#define mtx_init(m,o,n) _mtx_init(m, o, n)
#define mtx_destroy(m) _mtx_destroy(m)
#define mtx_locked(m) _mtx_locked(m)
#define mtx_owned(m) _mtx_owned(m)

#define mtx_assert(m, w) _mtx_assert(m, w, __FILE__, __LINE__)
#define mtx_trylock(m) _mtx_trylock(m, __FILE__, __LINE__)
#define mtx_lock(m) _mtx_lock(m, __FILE__, __LINE__)
#define mtx_unlock(m) _mtx_unlock(m)

#define mtx_spin_trylock(m) _mtx_spin_trylock(m, __FILE__, __LINE__)
#define mtx_spin_lock(m) _mtx_spin_lock(m, __FILE__, __LINE__)
#define mtx_spin_unlock(m) _mtx_spin_unlock(m)

#define mtx_alloc(o, n) ({ \
  mtx_t *m = kmalloc(sizeof(mtx_t)); \
  mtx_init(m, o, n); \
  m; \
})

#define mtx_free(m) ({ \
  mtx_destroy(m); \
  kfree((void *)(m)); \
  (m) = NULL; \
})

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
  struct lock_object lo;          // common lock object
  volatile uintptr_t owner_opts;  // owner thread ptr (8-byte aligned) bitwise
                                  // or'd with the rwlock options
  volatile uint32_t readers;      // number of readers
} rwlock_t;

// rwlock options
#define RWL_DEBUG   0x4  // enable debugging for this lock
#define RWL_RECURSE 0x2  // allow recursive locking
#define RWL_OPT_MASK 0x7

#undef embed_lock_object
#endif
