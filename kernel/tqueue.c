//
// Created by Aaron Gill-Braun on 2023-12-23.
//

#include <kernel/tqueue.h>
#include <kernel/proc.h>
#include <kernel/sched.h>
#include <kernel/panic.h>
#include <kernel/mm.h>
#include <kernel/alarm.h>
#include <kernel/errno.h>

#define ASSERT(x) kassert(x)

// =================================
//            runqueue
// =================================

void runq_init(struct runqueue *runq) {
  mtx_init(&runq->lock, MTX_SPIN, "runqueue_lock");
  LIST_INIT(&runq->head);
}

void runq_add(struct runqueue *runq, thread_t *td) {
  td_lock_assert(td, MA_LOCKED);

  mtx_spin_lock(&runq->lock);
  LIST_ADD(&runq->head, td, rqlist);
  atomic_fetch_add(&runq->count, 1);
  mtx_spin_unlock(&runq->lock);

  td->runq = runq;
}

void runq_remove(struct runqueue *runq, thread_t *td, bool *empty) {
  td_lock_assert(td, MA_LOCKED);

  size_t count;
  mtx_spin_lock(&runq->lock);
  LIST_REMOVE(&runq->head, td, rqlist);
  count = atomic_fetch_sub(&runq->count, 1);
  mtx_spin_unlock(&runq->lock);

  td->runq = NULL;
  if (empty != NULL) {
    *empty = count == 0;
  }
}

__locked thread_t *runq_next_thread(struct runqueue *runq, bool *empty) {
  size_t count;
  mtx_spin_lock(&runq->lock);

  thread_t *td = LIST_FIND(_td, &runq->head, rqlist, !TDF2_IS_STOPPED(_td));
  if (td != NULL) {
    ASSERT(runq->count > 0);
    LIST_REMOVE(&runq->head, td, rqlist);
    count = atomic_fetch_sub(&runq->count, 1);

    // lock the thread (and return it locked)
    td_lock(td);
    td->runq = NULL;
  }

  if (empty != NULL) {
    *empty = count == 0;
  }
  mtx_spin_unlock(&runq->lock);
  return td;
}

// =================================
//            lockqueue
// =================================

#define LQ_ASSERT(x) kassert(x)
#define LQ_DPRINTF(fmt, ...) kprintf("lockqueue: " fmt, ##__VA_ARGS__)

#define LQC_TABLESIZE 64 // must be power of 2
#define LQC_HASH(lock_obj) (((uintptr_t)(lock_obj) >> 4) & (LQC_TABLESIZE - 1))
#define LQC_LOOKUP(lock_obj) (&lqc_table[LQC_HASH(lock_obj)])

struct lockqueue_chain {
  struct mtx lock; // chain spin lock
  LIST_HEAD(struct lockqueue) head;
  LIST_HEAD(struct lockqueue) free;
};
static struct lockqueue_chain lqc_table[LQC_TABLESIZE];
static mtx_t td_contested_lock;

static void lockq_early_init() {
  mtx_init(&td_contested_lock, MTX_SPIN, "td_contested_lock");
  for (int i = 0; i < LQC_TABLESIZE; i++) {
    struct lockqueue_chain *chain = &lqc_table[i];
    mtx_init(&chain->lock, MTX_SPIN, "lockqueue_chain_lock");
  }
}
EARLY_INIT(lockq_early_init);

static void lockq_remove_thread(struct lockqueue_chain *chain, struct lockqueue *lockq, thread_t *td, int queue) {
  td_lock_assert(td, MA_OWNED);

  // remove the thread from the queue
  LIST_REMOVE(&lockq->queues[queue], td, lqlist);

  // find a spare lockqueue to give back to the thread
  struct lockqueue *own_lockq = NULL;
  if (LIST_EMPTY(&lockq->queues[LQ_EXCL])) {
    // no more threads in the lockqueue, we can take this one
    own_lockq = LIST_REMOVE(&chain->head, lockq, chain_list);
  } else {
    // take a lockqueue from the free list
    own_lockq = LIST_FIRST(&chain->free);
    LQ_ASSERT(own_lockq != NULL);
    LIST_REMOVE(&chain->free, own_lockq, chain_list);
  }

  td->own_lockq = own_lockq;
  td->contested_lock = NULL;
  
  // clear the lock_obj when giving back the lockqueue to thread
  if (own_lockq != NULL) {
    own_lockq->lock_obj = NULL;
  }
}

static void lockq_propagate_priorirty(struct lockqueue *lockq, thread_t *td) {
  todo();
}

static void lockq_unblock_thread(struct lockqueue *lockq, thread_t *td) {
  todo();
}

//

struct lockqueue *lockq_alloc() {
  struct lockqueue *lockq = kmallocz(sizeof(struct lockqueue));
  mtx_init(&lockq->lock, MTX_SPIN, "lockqueue_lock");
  return lockq;
}

void lockq_free(struct lockqueue **lockqp) {
  struct lockqueue *lockq = *moveptr(lockqp);
  mtx_destroy(&lockq->lock);
  kfree(lockq);
}

struct lockqueue *lockq_lookup(struct lock_object *lock_obj) {
  struct lockqueue_chain *chain = LQC_LOOKUP(lock_obj);
  mtx_spin_lock(&chain->lock);

  struct lockqueue *lockq = NULL;
  LIST_FOR_IN(lq, &chain->head, chain_list) {
    if (lq->lock_obj == lock_obj) {
      mtx_spin_lock(&lq->lock);
      lockq = lq;
      break;
    }
  }

  if (lockq == NULL) {
    mtx_spin_unlock(&chain->lock);
  }
  return lockq;
}

struct lockqueue *lockq_lookup_or_default(struct lock_object *lock_obj, struct lockqueue *default_lockq) {
  struct lockqueue_chain *chain = LQC_LOOKUP(lock_obj);
  mtx_spin_lock(&chain->lock);

  // locate an existing lockqueue
  struct lockqueue *lockq = NULL;
  LIST_FOREACH(lockq, &chain->head, chain_list) {
    if (lockq->lock_obj == lock_obj) {
      mtx_spin_lock(&lockq->lock);
      break;
    }
  }

  if (lockq == NULL) {
    ASSERT(default_lockq != NULL);
    // use the default lockq
    lockq = default_lockq;
    mtx_spin_lock(&lockq->lock);
    lockq->lock_obj = lock_obj;
    lockq->owner = NULL;
  }
  return lockq;
}

void lockq_release(struct lockqueue **lockqp) {
  struct lockqueue *lockq = moveptr(*lockqp);
  struct lockqueue_chain *chain = LQC_LOOKUP(lockq->lock_obj);
  mtx_assert(&chain->lock, MA_OWNED);
  mtx_assert(&lockq->lock, MA_OWNED);

  mtx_spin_unlock(&lockq->lock);
  mtx_spin_unlock(&chain->lock);
}

void lockq_chain_lock(struct lockqueue *lockq) {
  struct lockqueue_chain *chain = LQC_LOOKUP(lockq->lock_obj);
  mtx_spin_lock(&chain->lock);

  // lock the lockqueue
  mtx_spin_lock(&lockq->lock);
}

void lockq_chain_unlock(struct lockqueue *lockq) {
  struct lockqueue_chain *chain = LQC_LOOKUP(lockq->lock_obj);
  mtx_assert(&chain->lock, MA_OWNED);
  mtx_assert(&lockq->lock, MA_OWNED);

  // unlock the lockqueue
  mtx_spin_unlock(&lockq->lock);
  mtx_spin_unlock(&chain->lock);
}

void lockq_wait(struct lockqueue *lockq, struct thread *owner, int queue) {
  LQ_ASSERT(queue == LQ_EXCL);
  struct lockqueue_chain *chain = LQC_LOOKUP(lockq->lock_obj);
  mtx_assert(&chain->lock, MA_OWNED);
  mtx_assert(&lockq->lock, MA_OWNED);

  thread_t *td = curthread;
  td_lock(td);

  // set the owner if not already set (for newly created lockqueues)
  if (lockq->owner == NULL) {
    lockq->owner = owner;
  }

  if (lockq->owner != NULL && td->priority < lockq->owner->priority) {
    // propagate the priority to the lockqueue
//    lockq_propagate_priorirty(lockq, td);
  }

  if (lockq == td->own_lockq) {
    // the lockq was donated by us
    td->own_lockq = NULL;
    //  - the lock object should already be set
    ASSERT(lockq->lock_obj != NULL);
    //  - insert the lockq into the chain
    LIST_ADD(&chain->head, lockq, chain_list);
    //  - add the thread to the queue
    LIST_ADD(&lockq->queues[queue], td, lqlist);
  } else {
    // we are queueing onto an existing lockq
    //  - donate ours to the free list
    LIST_ADD(&chain->free, moveptr(td->own_lockq), chain_list);
    //  - add thread the queue (priority order)
    LIST_INSERT_ORDERED_BY(&lockq->queues[queue], td, lqlist, priority);
  }

  ASSERT(td->own_lockq == NULL);
  td->contested_lock = lockq->lock_obj;
  td->lockq_num = queue;
  // todo: track blocked time

  mtx_spin_unlock(&lockq->lock);
  mtx_spin_unlock(&chain->lock);
  // td is still locked
  sched_again(SCHED_BLOCKED);
}

void lockq_remove(struct lockqueue *lockq, struct thread *td, int queue) {
  LQ_ASSERT(TDS_IS_BLOCKED(td));
  LQ_ASSERT(queue == LQ_EXCL);
  struct lockqueue_chain *chain = LQC_LOOKUP(lockq->lock_obj);
  mtx_assert(&chain->lock, MA_OWNED);
  mtx_assert(&lockq->lock, MA_OWNED);

  lockq_remove_thread(chain, lockq, td, queue);

  mtx_spin_unlock(&lockq->lock);
  mtx_spin_unlock(&chain->lock);
}

void lockq_signal(struct lockqueue *lockq, int queue) {
  LQ_ASSERT(queue == LQ_EXCL);
  struct lockqueue_chain *chain = LQC_LOOKUP(lockq->lock_obj);
  mtx_assert(&chain->lock, MA_OWNED);
  mtx_assert(&lockq->lock, MA_OWNED);

  thread_t *td = LIST_FIRST(&lockq->queues[queue]);
  while (td != NULL) {
    td_lock(td);
    thread_t *next = LIST_NEXT(td, lqlist);

    // unblock the thread
    lockq_remove_thread(chain, lockq, td, queue);

    TD_SET_STATE(td, TDS_READY);
    sched_submit_ready_thread(td);
    td_unlock(td);
    td = next;
  }

  mtx_spin_unlock(&lockq->lock);
  mtx_spin_unlock(&chain->lock);
}

void lockq_update_priority(struct lockqueue *lockq, struct thread *td) {
  mtx_assert(&lockq->lock, MA_OWNED);


}

// =================================
//            waitqueue
// =================================

#define WQ_ASSERT(x) kassert(x)
#define WQ_DPRINTF(fmt, ...)
// #define WQ_DPRINTF(fmt, ...) kprintf("waitqueue: " fmt, ##__VA_ARGS__)

#define WQC_TABLESIZE 64 // must be power of 2
#define WQC_HASH(wchan) (((uintptr_t)(wchan) >> 4) & (WQC_TABLESIZE - 1))
#define WQC_LOOKUP(wchan) (&waitq_chains[WQC_HASH(wchan)])

struct waitqueue_chain {
  struct mtx lock; // chain spin lock
  LIST_HEAD(struct waitqueue) head;
  LIST_HEAD(struct waitqueue) free;
};
static struct waitqueue_chain waitq_chains[WQC_TABLESIZE];

static void waitq_early_init() {
  for (int i = 0; i < WQC_TABLESIZE; i++) {
    struct waitqueue_chain *chain = &waitq_chains[i];
    mtx_init(&chain->lock, MTX_SPIN, "waitqueue_chain_lock");
    LIST_INIT(&chain->head);
  }
}
EARLY_INIT(waitq_early_init);


static void waitq_add_internal(struct waitqueue *waitq, struct waitqueue_chain *chain, thread_t *td, const char *wdmsg) {
  td_lock_assert(td, MA_OWNED);
  if (waitq == td->own_waitq) {
    // the waitq was donated by us
    td->own_waitq = NULL;
    //  - insert the waitq into the chain
    LIST_ADD(&chain->head, waitq, chain_list);
  } else {
    // we are queueing onto an existing waitq
    //  - donate ours to the free list
    LIST_ADD(&chain->free, moveptr(td->own_waitq), chain_list);
  }
  //  - add the thread to the queue
  LIST_ADD(&waitq->queue, td, wqlist);

  td->wchan = waitq->wchan;
  td->wdmsg = wdmsg;
}

static void waitq_remove_internal(struct waitqueue *waitq, struct waitqueue_chain *chain, thread_t *td) {
  td_lock_assert(td, MA_OWNED);

  // remove the thread from the queue
  LIST_REMOVE(&waitq->queue, td, wqlist);

  // find a spare waitqueue to give to the thread
  struct waitqueue *own_waitq = NULL;
  if (LIST_EMPTY(&waitq->queue)) {
    // no more threads in the waitqueue, we can take this one
    own_waitq = LIST_REMOVE(&chain->head, waitq, chain_list);
  } else {
    // take a waitqueue from the free list
    own_waitq = LIST_REMOVE(&chain->free, own_waitq, chain_list);
    ASSERT(own_waitq != NULL);
  }

  td->own_waitq = own_waitq;
  td->wchan = NULL;
  td->wdmsg = NULL;
}

static void waitq_timeout_callback(alarm_t *alarm, thread_t *td) {
  // remove the thread from the waitqueue if it's still waiting
  if (!TDS_IS_WAITING(td)) {
    // thread already woken up normally, nothing to do
    return;
  }

  struct waitqueue *waitq = waitq_lookup(td->wchan);
  if (waitq == NULL) {
    // waitqueue already destroyed
    return;
  }

  td_lock(td);
  if (td->wchan == NULL || !TDS_IS_WAITING(td)) {
    // thread was already woken up, race condition
    td_unlock(td);
    waitq_release(&waitq);
    return;
  }

  // remove the thread from the waitqueue
  waitq_remove_internal(waitq, WQC_LOOKUP(waitq->wchan), td);

  // mark that we timed out
  td->errno = ETIMEDOUT;

  // make thread ready
  TD_SET_STATE(td, TDS_READY);
  sched_submit_ready_thread(td);
  td_unlock(td);

  waitq_release(&waitq);
}

//

struct waitqueue *waitq_alloc() {
  struct waitqueue *waitq = kmallocz(sizeof(struct waitqueue));
  mtx_init(&waitq->lock, MTX_SPIN, "waitqueue_lock");
  return waitq;
}

void waitq_free(struct waitqueue **waitqp) {
  struct waitqueue *waitq = *moveptr(waitqp);
  mtx_destroy(&waitq->lock);
  kfree(waitq);
}

struct waitqueue *waitq_lookup(const void *wchan) {
  if (wchan == NULL) {
    return NULL; // no waitqueue for NULL wchan
  }

  struct waitqueue_chain *chain = WQC_LOOKUP(wchan);
  mtx_spin_lock(&chain->lock);

  struct waitqueue *waitq = NULL;
  LIST_FOR_IN(wq, &chain->head, chain_list) {
    if (wq->wchan == wchan) {
      mtx_spin_lock(&wq->lock);
      waitq = wq;
      break;
    }
  }

  if (waitq == NULL) {
    mtx_spin_unlock(&chain->lock);
  }
  WQ_DPRINTF("waitq_lookup: wchan=%p, waitq=%p\n", wchan, waitq);
  return waitq;
}

void waitq_release(struct waitqueue **waitqp) {
  struct waitqueue *waitq = moveptr(*waitqp);
  if (waitq == NULL) {
    return; // nothing to release
  }

  struct waitqueue_chain *chain = WQC_LOOKUP(waitq->wchan);
  mtx_assert(&chain->lock, MA_OWNED);
  mtx_assert(&waitq->lock, MA_OWNED);

  mtx_spin_unlock(&waitq->lock);
  mtx_spin_unlock(&chain->lock);
  WQ_DPRINTF("waitq_release: wchan=%p, waitq=%p\n", waitq->wchan, waitq);
}

struct waitqueue *waitq_lookup_or_default(int type, const void *wchan, struct waitqueue *default_waitq) {
  ASSERT(type == WQ_SLEEP || type == WQ_CONDV || type == WQ_SEMA);
  struct waitqueue_chain *chain = WQC_LOOKUP(wchan);
  mtx_spin_lock(&chain->lock);

  // locate an existing waitqueue
  struct waitqueue *waitq = NULL;
  LIST_FOREACH(waitq, &chain->head, chain_list) {
    if (waitq->wchan == wchan) {
      ASSERT(waitq->type == type);
      mtx_spin_lock(&waitq->lock);
      break;
    }
  }

  if (waitq == NULL && default_waitq != NULL) {
    // use the default waitq
    waitq = default_waitq;
    mtx_spin_lock(&waitq->lock);
    waitq->type = type;
    waitq->wchan = wchan;
  }
  return waitq;
}

void waitq_wait(struct waitqueue *waitq, const char *wdmsg) {
  struct waitqueue_chain *chain = WQC_LOOKUP(waitq->wchan);
  mtx_assert(&chain->lock, MA_OWNED);
  mtx_assert(&waitq->lock, MA_OWNED);

  thread_t *td = curthread;
  td_lock(td);
  waitq_add_internal(waitq, chain, td, wdmsg);
  mtx_spin_unlock(&waitq->lock);
  mtx_spin_unlock(&chain->lock);
  WQ_DPRINTF("waitq_wait: wchan=%p, waitq=%p, wdmsg=%s, td={:td}\n", waitq->wchan, waitq, wdmsg, td);

  // td is locked on call to sched_again
  sched_again(SCHED_SLEEPING);
}

int waitq_wait_timeout(struct waitqueue *waitq, const char *wdmsg, uint64_t timeout_ns) {
  struct waitqueue_chain *chain = WQC_LOOKUP(waitq->wchan);
  mtx_assert(&chain->lock, MA_OWNED);
  mtx_assert(&waitq->lock, MA_OWNED);
  
  // create timeout alarm
  thread_t *td = curthread;
  alarm_t *alarm = alarm_alloc_relative(timeout_ns, alarm_cb(waitq_timeout_callback, td));
  if (alarm == NULL) {
    return -ENOMEM;
  }

  td_lock(td);
  ASSERT(td->timeout_alarm_id == 0);

  id_t alarm_id = alarm_register(alarm);
  td->timeout_alarm_id = alarm_id;
  td->errno = 0;
  waitq_add_internal(waitq, chain, td, wdmsg);

  mtx_spin_unlock(&waitq->lock);
  mtx_spin_unlock(&chain->lock);
  WQ_DPRINTF("waitq_wait_timeout: wchan=%p, waitq=%p, wdmsg=%s, timeout_ns=%llu, td={:td}\n", 
             waitq->wchan, waitq, wdmsg, timeout_ns, td);

  // td is locked on call to sched_again
  sched_again(SCHED_SLEEPING);
  // and returns with the thread unlocked, re-acquire it
  td_lock(td);

  // we're back - check if we timed out
  int ret = 0;
  if (td->errno == ETIMEDOUT) {
    ret = -ETIMEDOUT;
    td->errno = 0;
  } else {
    // normal wakeup, cancel the alarm
    if (td->timeout_alarm_id != 0) {
      alarm_unregister(td->timeout_alarm_id);
    }
  }
  td->timeout_alarm_id = 0;

  td_unlock(td);
  return ret;
}

int waitq_wait_sig(struct waitqueue *waitq, const char *wdmsg) {
  struct waitqueue_chain *chain = WQC_LOOKUP(waitq->wchan);
  mtx_assert(&chain->lock, MA_OWNED);
  mtx_assert(&waitq->lock, MA_OWNED);

  thread_t *td = curthread;
  td_lock(td);

  td->flags2 |= TDF2_WAKEABLE;
  td->errno = 0;
  waitq_add_internal(waitq, chain, td, wdmsg);

  mtx_spin_unlock(&waitq->lock);
  mtx_spin_unlock(&chain->lock);

  // td is locked on call to sched_again
  sched_again(SCHED_SLEEPING);
  // and returns with the thread unlocked, re-acquire it
  td_lock(td);

  // we're back - clear the wakeable flag
  td->flags2 &= ~TDF2_WAKEABLE;

  // check if we were interrupted
  int ret = 0;
  if (td->errno == EINTR) {
    ret = -EINTR;
    td->errno = 0;
  }

  td_unlock(td);
  return ret;
}

int waitq_wait_sigtimeout(struct waitqueue *waitq, const char *wdmsg, uint64_t timeout_ns) {
  struct waitqueue_chain *chain = WQC_LOOKUP(waitq->wchan);
  mtx_assert(&chain->lock, MA_OWNED);
  mtx_assert(&waitq->lock, MA_OWNED);
  
  // create timeout alarm
  thread_t *td = curthread;
  alarm_t *alarm = alarm_alloc_relative(timeout_ns, alarm_cb(waitq_timeout_callback, td));
  if (alarm == NULL) {
    return -ENOMEM;
  }

  td_lock(td);
  ASSERT(td->timeout_alarm_id == 0);

  id_t alarm_id = alarm_register(alarm);
  td->timeout_alarm_id = alarm_id;
  td->errno = 0;
  td->flags2 |= TDF2_WAKEABLE;  // mark as interruptible by signals
  waitq_add_internal(waitq, chain, td, wdmsg);

  mtx_spin_unlock(&waitq->lock);
  mtx_spin_unlock(&chain->lock);
  WQ_DPRINTF("waitq_wait_sigtimeout: wchan=%p, waitq=%p, wdmsg=%s, timeout_ns=%llu, td={:td}\n", 
             waitq->wchan, waitq, wdmsg, timeout_ns, td);

  // td is locked on call to sched_again
  sched_again(SCHED_SLEEPING);
  // and returns with the thread unlocked, re-acquire it
  td_lock(td);

  // we're back - clear the wakeable flag
  td->flags2 &= ~TDF2_WAKEABLE;

  // check what happened
  int ret = 0;
  if (td->errno == ETIMEDOUT) {
    ret = -ETIMEDOUT;
    td->errno = 0;
  } else if (td->errno == EINTR) {
    ret = -EINTR;
    td->errno = 0;
    // signal interrupted us, cancel the alarm
    if (td->timeout_alarm_id != 0) {
      alarm_unregister(td->timeout_alarm_id);
    }
  } else {
    // normal wakeup, cancel the alarm
    if (td->timeout_alarm_id != 0) {
      alarm_unregister(td->timeout_alarm_id);
    }
  }
  td->timeout_alarm_id = 0;

  td_unlock(td);
  return ret;
}

void waitq_remove(struct waitqueue *waitq, thread_t *td) {
  WQ_ASSERT(TDS_IS_WAITING(td));
  struct waitqueue_chain *chain = WQC_LOOKUP(waitq->wchan);
  mtx_assert(&chain->lock, MA_OWNED);
  mtx_assert(&waitq->lock, MA_OWNED);

  waitq_remove_internal(waitq, chain, td);

  mtx_spin_unlock(&waitq->lock);
  mtx_spin_unlock(&chain->lock);
  WQ_DPRINTF("waitq_remove: wchan=%p, waitq=%p, td={:td}\n", waitq->wchan, waitq, td);
}

void waitq_signal(struct waitqueue *waitq) {
  struct waitqueue_chain *chain = WQC_LOOKUP(waitq->wchan);
  mtx_assert(&chain->lock, MA_OWNED);
  mtx_assert(&waitq->lock, MA_OWNED);

  bool preempt = false;
  thread_t *td = LIST_FIRST(&waitq->queue);
  if (td != NULL) {
    td_lock(td);
    waitq_remove_internal(waitq, chain, td);
    TD_SET_STATE(td, TDS_READY);
    sched_submit_ready_thread(td);
    td_unlock(td);

    if (td->priority > curthread->priority && td->cpu_id == curcpu_id) {
      preempt = true;
    }
  }

  WQ_DPRINTF("waitq_signal: wchan=%p, waitq=%p, td={:td}\n", waitq->wchan, waitq, td);
  mtx_spin_unlock(&waitq->lock);
  mtx_spin_unlock(&chain->lock);
  if (preempt) {
    sched_again(SCHED_PREEMPTED);
  }
}

void waitq_broadcast(struct waitqueue *waitq) {
  struct waitqueue_chain *chain = WQC_LOOKUP(waitq->wchan);
  mtx_assert(&chain->lock, MA_OWNED);
  mtx_assert(&waitq->lock, MA_OWNED);
  WQ_DPRINTF("waitq_broadcast: wchan=%p, waitq=%p\n", waitq->wchan, waitq);

  bool preempt = false;
  thread_t *td = NULL;
  while ((td = LIST_FIRST(&waitq->queue))) {
    td_lock(td);
    waitq_remove_internal(waitq, chain, td);
    TD_SET_STATE(td, TDS_READY);
    sched_submit_ready_thread(td);
    td_unlock(td);

    if (td->priority > curthread->priority && td->cpu_id == curcpu_id) {
      preempt = true;
    }
  }

  mtx_spin_unlock(&waitq->lock);
  mtx_spin_unlock(&chain->lock);

  if (preempt) {
    sched_again(SCHED_PREEMPTED);
  }
}
