//
// Created by Aaron Gill-Braun on 2023-12-23.
//

#ifndef KERNEL_TQUEUE_H
#define KERNEL_TQUEUE_H

#include <kernel/lock.h>

// https://man.freebsd.org/cgi/man.cgi?locking(9)

typedef LIST_HEAD(struct thread) tdqueue_t;

// =================================
//            runqueue
// =================================

struct runqueue {
  struct mtx lock;
  size_t count;
  tdqueue_t queue;
};

void runq_init(struct runqueue *runq);
/// Adds the given thread to the runqueue. The thread lock should be held when
/// calling this function, and it will be released after the thread has been
/// added to the queue and transitioned to the ready state.
void runq_add(struct runqueue *runq, thread_t *td);
/// Removes the given thread from the runqueue. The thread lock should be held
/// when calling this function, and it will remain locked on return.
void runq_remove(struct runqueue *runq, thread_t *td);
/// Returns the next thread to run from the runqueue. If this function returns
/// a non-null value, the thread lock will be held and the thread will be in the
/// ready state.
thread_t *runq_remove_next(struct runqueue *runq);

// =================================
//            lockqueue
// =================================

// https://cgit.freebsd.org/src/tree/sys/sys/turnstile.h

#define LQ_EXCL 0 // exclusive access queue
#define LQ_SHRD 1 // shared access queue

/*
 * A lockqueue is a queue for threads waiting on lock access.
 *
 * Lockqueues are used by short-term locks (non-spin mtx, rwlock) to mediate
 * access to the inner lock. It is equivalent to the turnstile in FreeBSD.
 */
struct lockqueue {
  struct mtx lock;                          // lockqueue struct spinlock
  tdqueue_t queues[2];                      // exclusive and shared queues

  struct thread *owner;                     // owning thread
  LIST_ENTRY(struct lockqueue) td_list;     // thread contested list entry
  struct lock_object *lock_obj;             // the lock object
  LIST_ENTRY(struct lockqueue) chain_list;  // chain list entry
};

struct lockqueue *lockq_alloc();
void lockq_free(struct lockqueue **lockqp);

/// Locates the lockqueue associated with the given lock object.
struct lockqueue *lockq_lookup(struct lock_object *lock_obj);
/// Locks the lockqueue chain lock associated with the given lock object.
void lockq_chain_lock(struct lock_object *lock_obj);
/// Unlocks the lockqueue chain lock associated with the given lock object.
void lockq_chain_unlock(struct lock_object *lock_obj);

/// Blocks the current thread on the lockqueue. This function will context switch
/// and not return until it has been woken back up. This should be called with the
/// chain lock asscoiated with the lockqueue held, and will return with it unlocked.
void lockq_wait(struct lockqueue *lockq, struct thread *owner, int queue);
/// Removes the given thread from the lockqueue.
void lockq_remove(struct lockqueue *lockq, struct thread *td, int queue);

/// Signals the first thread on the lockqueue and unblocks it.
void lockq_signal(struct lockqueue *lockq, int queue);
/// Signals all threads on the lockqueue and unblocks them.
void lockq_broadcast(struct lockqueue *lockq, int queue);


// =================================
//            waitqueue
// =================================

// https://cgit.freebsd.org/src/tree/sys/sys/sleepqueue.h

#define WQ_SLEEP  0x1 // wchan is a sleep channel
#define WQ_CONDV  0x2 // wchan is cond var channel

/*
 * A waitqueue is a queue for threads waiting on a condition.
 *
 * Equivalent to FreeBSD sleepqueues.
 */
struct waitqueue {
  struct mtx lock;    // waitqueue struct spinlock
  tdqueue_t queue;    // thread queue
  const void *wchan;  // wait channel
  LIST_ENTRY(struct waitqueue) chain_list;
};

struct waitqueue *waitq_alloc();
void waitq_free(struct waitqueue **waitqp);

/// Locates the sleepqueue associated with the given wait channel.
struct waitqueue *waitq_lookup(const void *wchan);
/// Locks the sleepqueue chain lock associated with the given wait channel.
void waitq_chain_lock(const void *wchan);
/// Unlocks the sleepqueue chain lock associated with the given wait channel.
void waitq_chain_unlock(const void *wchan);

/// Blocks the current thread on the waitqueue. This function will context switch
/// and not return until it has been woken back up. This should be called with the
/// chain lock asscoiated with the waitqueue held, and will return with it unlocked.
void waitq_wait(struct waitqueue *waitq, const char *wdmsg);
/// Removes the given thread from the waitqueue.
void waitq_remove(struct waitqueue *waitq, struct thread *td);



#endif
