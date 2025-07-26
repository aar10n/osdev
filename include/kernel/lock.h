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
struct lock_claim;
struct lock_claim_list;

/*
 * Common lock object.
 *
 * All lock_class lock objects have a lock_object as their first member. There are
 * some common state bits shared among all lock types with the rest being free for
 * each lock_class to use. The data field is also for implementation use.
 */
struct lock_object {
  const char *name;         /* lock name */
  uint32_t flags;           /* lock options + class bits */
  uint32_t data;            /* lock class data */
};

typedef void (*lockclass_lock_t)(struct lock_object *lock, uintptr_t how, const char *file, int line);
typedef void (*lockclass_unlock_t)(struct lock_object *lock, const char *file, int line);
typedef void (*lockclass_assert_t)(struct lock_object *lock, int what, const char *file, int line);
typedef struct thread *(*lockclass_owner_t)(struct lock_object *lock);

/*
 * A generic interface for the different lock types (mutex, rwlock).
 */
struct lock_class {
  const char *name;
  uint32_t flags;                    // lock class flags (LC_*)
  lockclass_lock_t lc_lock;          // lock function
  lockclass_unlock_t lc_unlock;      // unlock function
  lockclass_assert_t lc_assert;      // assert function
  lockclass_owner_t lc_owner;        // owner function
};

#define NUM_LOCK_CLASSES 3

#define lockclass_lock(lc, lo, how) (lc)->lc_lock((lo), (how), __FILE__, __LINE__)
#define lockclass_unlock(lc, lo) (lc)->lc_unlock((lo), __FILE__, __LINE__)
#define lockclass_assert(lc, lo, what) (lc)->lc_assert((lo), (what), __FILE__, __LINE__)
#define lockclass_owner(lc, lo) (lc)->lc_owner((lo))

// === lock class flags ===
// Each lock class has a unique set of flags that are used to
// identify and describe the behavior of the lock. The class
// bits are available in the state of every lock of that type.
#define LC_SPINLOCK     0x0001  // spin lock
#define LC_WAITLOCK     0x0002  // wait lock
#define LC_SHAREABLE    0x0004  // lock can have shared ownership
#define LC_SLEEPABLE    0x0008  // can sleep while holding lock
#define LC_MASK   0x00ff

// === lock option flags ===
// (these are OR'd with the class bits)
// These flags describe common properties/state that are used by
// all lock classes. Each lock class nomalizes its specific init
// flags into these to store in state.
#define LO_DEBUG        0x0100 // enable debugging for this lock
#define LO_NOCLAIMS     0x0200 // don't track lock claims
#define LO_RECURSABLE   0x0400 // lock is recursable
#define LO_SLEEPABLE    0x0800 // can sleep while holding lock
#define LO_INITIALIZED  0x1000 // lock has been initialized
#define LO_FLAGS_MASK   0xffff

// === lock class 'how' flags ===
#define LC_EXCL         0x0000 // exclusive lock
#define LC_SHARED       0x0001 // shared lock

// === lock assertions ===
#define LA_UNLOCKED     0x00 // lock is not locked
#define LA_LOCKED       0x01 // lock is locked (any s/x)
#define LA_SLOCKED      0x02 // lock is shared locked
#define LA_XLOCKED      0x04 // lock is exclusive locked
#define LA_OWNED        0x08 // lock is locked and owned by thread
#define LA_NOTOWNED     0x10 // lock may be locked but not owned by thread
#define LA_RECURSED     0x20 // lock is recursed
#define LA_NOTRECURSED  0x40 // lock is not recursed
#define LA_INITIALIZED  0x80 // lock is initialized

#define LO_LOCK_CLASS(lock) ((lock)->flags & LC_MASK)
#define LO_LOCK_OPTS(lock) ((lock)->flags & LO_FLAGS_MASK)

#define SPINLOCK_LOCKCLASS  (LC_SPINLOCK)
#define MUTEX_LOCKCLASS     (LC_WAITLOCK)
#define RWLOCK_LOCKCLASS    (LC_WAITLOCK|LC_SHAREABLE)

extern struct lock_class *lock_classes[];

static inline uint8_t lock_class_index(uint32_t lc_flags) {
  if (lc_flags & SPINLOCK_LOCKCLASS) {
    return 0;
  } else if (lc_flags & MUTEX_LOCKCLASS) {
    return 1;
  } else if (lc_flags & RWLOCK_LOCKCLASS) {
    return 2;
  } else {
    panic("lock_class_flags_to_index() called with unknown lockclass %x", lc_flags);
  }
}

static inline struct lock_class *lock_class_lookup(uint32_t lc_flags) {
  struct lock_class *lc;
  if (lc_flags & SPINLOCK_LOCKCLASS) {
    lc = lock_classes[lock_class_index(SPINLOCK_LOCKCLASS)];
  } else if (lc_flags & MUTEX_LOCKCLASS) {
    lc = lock_classes[lock_class_index(MUTEX_LOCKCLASS)];
  } else if (lc_flags & RWLOCK_LOCKCLASS) {
    lc = lock_classes[lock_class_index(RWLOCK_LOCKCLASS)];
  } else {
    panic("lock_class_lookup() called with unknown lockclass %x", lc_flags);
  }

  if (lc == NULL) {
    panic("lock_class_lookup() called with uninitialized lockclass %x", lc_flags);
  }
  return lc;
}

static inline const char *lock_class_kind_str(uint32_t lc_flags) {
  if (lc_flags == SPINLOCK_LOCKCLASS) {
    return "spinlock";
  } else if (lc_flags == MUTEX_LOCKCLASS) {
    return "mutex";
  } else if (lc_flags == RWLOCK_LOCKCLASS) {
    return "rwlock";
  }
  panic("lock_class_kind_str() called with unknown lockclass %x", lc_flags);
}


//////////////////////////////////
// lockobject generic interface
typedef void (*lockclass_lock_t)(struct lock_object *lock, uintptr_t how, const char *file, int line);
typedef void (*lockclass_unlock_t)(struct lock_object *lock, const char *file, int line);
typedef void (*lockclass_assert_t)(struct lock_object *lock, int what, const char *file, int line);
typedef struct thread *(*lockclass_owner_t)(struct lock_object *lock);

////////////////////
// lock claim api
struct lock_claim_list *lock_claim_list_alloc();
void lock_claim_list_free(struct lock_claim_list **listp);
void lock_claim_list_add(struct lock_claim_list *list, struct lock_object *lock, uintptr_t how, const char *file, int line);
void lock_claim_list_remove(struct lock_claim_list *list, struct lock_object *lock);

////////////////////
// spin delay api

struct spin_delay {
  uint32_t delay_count;
  uint32_t max_waits;
  /* private */
  uint32_t waits;
};
#define new_spin_delay(count, maxwaits) ((struct spin_delay){ \
  .delay_count = (count), \
  .max_waits = (maxwaits), \
  .waits = 0, \
})

#define SHORT_DELAY 100
#define LONG_DELAY  1000
#define MAX_RETRIES UINT32_MAX

int spin_delay_wait(struct spin_delay *delay);

#endif
