//
// Created by Aaron Gill-Braun on 2023-12-23.
//

#ifndef KERNEL_TQUEUE_H
#define KERNEL_TQUEUE_H

#include <kernel/mutex.h>

// https://man.freebsd.org/cgi/man.cgi?locking(9)

typedef LIST_HEAD(struct thread) tdqueue_t;

// =================================
//            runqueue
// =================================

struct runqueue {
  mtx_t lock;
  size_t count;
  tdqueue_t head;
};

void runq_init(struct runqueue *runq);

/// Adds the given thread to the runqueue. The thread lock should be held when
/// calling this function, and will remain locked on return.
void runq_add(struct runqueue *runq, __locked struct thread *td);

/// Removes the given thread from the runqueue. The thread lock should be held
/// when calling this function, and it will remain locked on return.
void runq_remove(struct runqueue *runq, __locked struct thread *td, bool *empty);

/// Removes and returns the next thread to run from the runqueue. If this function
/// returns a non-null thread, the thread lock will be held and it will be in the
/// running state.
__locked struct thread *runq_next_thread(struct runqueue *runq, bool *empty);

// =================================
//            lockqueue
// =================================

// https://cgit.freebsd.org/src/tree/sys/sys/turnstile.h

#define LQ_EXCL 0 // exclusive access queue
// todo: LQ_SHRD

/*
* A lockqueue is a queue for threads waiting on lock access.
*
* Lockqueues are used by short-term locks (non-spin mtx, rwlock) to mediate
* access to the inner lock. It is equivalent to the turnstile in FreeBSD.
*/
struct lockqueue {
  mtx_t lock;                               // lockqueue spin mutex
  tdqueue_t queues[2];                      // exclusive and shared queues

  struct thread *owner;                     // owning thread
  struct lock_object *lock_obj;             // the lock object
  LIST_ENTRY(struct lockqueue) chain_list;  // chain list entry
  LIST_ENTRY(struct lockqueue) claimed;     // thread claimed lockq list entry
};

struct lockqueue *lockq_alloc();
void lockq_free(struct lockqueue **lockqp);

/// Lockqueue Rules
//    -

/// Locates the lockqueue associated with the given lock object. It returns
/// the lockqueue with both its lock and associated chain lock held.
struct lockqueue *lockq_lookup(struct lock_object *lock_obj);

/// Locates the lockqueue associated with the given lock object. If one does
/// not already exist the default lockq is used. It returns the lockqueue with
/// both its lock and associated chain lock held.
struct lockqueue *lockq_lookup_or_default(struct lock_object *lock_obj, struct lockqueue *default_lockq);

/// Releases the lockq lock and the associated chain lock, moving the value
/// out of lockqp.
void lockq_release(struct lockqueue **lockqp);

void lockq_chain_lock(struct lockqueue *lockq);
void lockq_chain_unlock(struct lockqueue *lockq);

/// Blocks the calling thread on the lockqueue. This function will context switch
/// and not return until it has been woken back up (via lockq_signal);
void lockq_wait(struct lockqueue *lockq, struct thread *owner, int queue);

/// Removes the given thread from the lockqueue. This function should be called with
/// both the lock and chain lock held, and will return with both unlocked.
void lockq_remove(struct lockqueue *lockq, struct thread *td, int queue);

/// Unblocks the first thread on the lockqueue. This function should be called
/// with both the lock and chain lock held and will return with both unlocked.
void lockq_signal(struct lockqueue *lockq, int queue);

/// Updates the priority of the lockqueue to match the given thread. This
/// function should be called with the thread locked.
void lockq_update_priority(struct lockqueue *lockq, struct thread *td);

// =================================
//            waitqueue
// =================================

// https://cgit.freebsd.org/src/tree/sys/sys/sleepqueue.h

#define WQ_SLEEP  0x1 // wchan is a sleep channel
#define WQ_CONDV  0x2 // wchan is cond var channel
#define WQ_SEMA   0x3 // wchan is semaphore channel

/*
 * A waitqueue is a queue for threads waiting on a condition (or sleeping).
 *
 * Equivalent to FreeBSD sleepqueues.
 */
struct waitqueue {
  int type;             // waitqueue type (WQ_SLEEP or WQ_CONDV)
  mtx_t lock;           // waitqueue struct spinlock
  tdqueue_t queue;      // thread queue
  const void *wchan;    // wait channel
  LIST_ENTRY(struct waitqueue) chain_list;
};

struct waitqueue *waitq_alloc();
void waitq_free(struct waitqueue **waitqp);

/// Locates the sleepqueue associated with the given wait channel. It returns
/// the sleepqueue with both its lock and associated chain lock held.
struct waitqueue *waitq_lookup(const void *wchan);

/// Releases the waitq lock and the associated chain lock, moving the value
/// out of waitqp.
void waitq_release(struct waitqueue **waitqp);

/// Locates the sleepqueue associated with the given wait channel. If one does
/// not already exist the default sleepq is used. It returns the sleepqueue with
/// both its lock and associated chain lock held.
struct waitqueue *waitq_lookup_or_default(int type, const void *wchan, struct waitqueue *default_waitq);

/// Blocks the current thread on the waitqueue. This function will context switch
/// and not return until it has been woken back up. This should be called with
/// both the lock and chain lock held, and will return with both unlocked.
void waitq_wait(struct waitqueue *waitq, const char *wdmsg);

/// Blocks the current thread on the waitqueue with a timeout. This function will
/// context switch and not return until it has been woken back up or the timeout
/// expires. This should be called with both the lock and chain lock held, and will
/// return with both unlocked. Returns 0 on normal wakeup, -ETIMEDOUT on timeout.
int waitq_wait_timeout(struct waitqueue *waitq, const char *wdmsg, uint64_t timeout_ns);

/// Blocks the current thread on the waitqueue and allows it to be interrupted
/// by a signal. This function will context switch and not return until it has been
/// woken back up or interrupted by a signal. This should be called with both the
/// lock and chain lock held, and will return with both unlocked. Returns 0 on
/// normal wakeup, -EINTR if interrupted by a signal.
int waitq_wait_sig(struct waitqueue *waitq, const char *wdmsg);

/// Blocks the current thread on the waitqueue with a timeout and allows it to be
/// interrupted by a signal. This function will context switch and not return until
/// it has been woken back up, the timeout expires, or it is interrupted by a signal.
/// This should be called with both the lock and chain lock held, and will return
/// with both unlocked. Returns 0 on normal wakeup, -ETIMEDOUT on timeout, -EINTR
/// if interrupted by a signal.
int waitq_wait_sigtimeout(struct waitqueue *waitq, const char *wdmsg, uint64_t timeout_ns);

/// Removes the given thread from the waitqueue. This function should be called with
/// both the lock and chain lock held and will return with both unlocked.
void waitq_remove(struct waitqueue *waitq, struct thread *td);

/// Signals the first thread on the waitqueue and unblocks it. This function should
/// be called with both the lock and chain lock held and will return with both unlocked.
void waitq_signal(struct waitqueue *waitq);

/// Signals all threads on the waitqueue and unblocks them. If a higher priority thread
/// on the same cpu is woken up, it will preempt the current thread. This function should
/// be called with both the lock and chain lock held and will return with both unlocked.
void waitq_broadcast(struct waitqueue *waitq);

#endif
