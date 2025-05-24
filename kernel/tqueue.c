//
// Created by Aaron Gill-Braun on 2023-12-23.
//

#include <kernel/tqueue.h>
#include <kernel/proc.h>
#include <kernel/sched.h>
#include <kernel/panic.h>
#include <kernel/mm.h>

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
  LIST_ADD_FRONT(&runq->head, td, rqlist);
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

static void lockq_static_init() {
  mtx_init(&td_contested_lock, MTX_SPIN, "td_contested_lock");
  for (int i = 0; i < LQC_TABLESIZE; i++) {
    struct lockqueue_chain *chain = &lqc_table[i];
    mtx_init(&chain->lock, MTX_SPIN, "lockqueue_chain_lock");
  }
}
STATIC_INIT(lockq_static_init);

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
    own_lockq = LIST_REMOVE(&chain->free, own_lockq, chain_list);
    LQ_ASSERT(own_lockq != NULL);
  }

  td->own_lockq = own_lockq;
  td->contested_lock = NULL;
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
  mtx_init(&lockq->lock, 0, "lockqueue_lock");
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
  LIST_FOREACH(lockq, &chain->head, chain_list) {
    if (lockq->lock_obj == lock_obj) {
      mtx_spin_lock(&chain->lock);
      break;
    }
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

  if (lockq == NULL && default_lockq != NULL) {
    // use the default lockq
    lockq = default_lockq;
    mtx_spin_lock(&lockq->lock);
  }
  return lockq;
}

void lockq_release(struct lockqueue **lockqp) {
  struct lockqueue *lockq = *moveptr(lockqp);
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

  if (td->priority < lockq->owner->priority) {
    // propagate the priority to the lockqueue
    lockq_propagate_priorirty(lockq, td);
  }

  if (lockq == td->own_lockq) {
    // the lockq was donated by us
    td->own_lockq = NULL;
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
  while (true) {
    td_lock(td);
    if (TDF2_IS_STOPPED(td)) {
      td_unlock(td);
    }

    // unblock the thread
    lockq_remove_thread(chain, lockq, td, queue);

    TD_SET_STATE(td, TDS_READY);
    sched_submit_ready_thread(td);
    td_unlock(td);
    break;
  }
}

void lockq_update_priority(struct lockqueue *lockq, struct thread *td) {
  mtx_assert(&lockq->lock, MA_OWNED);


}

// =================================
//            waitqueue
// =================================

#define WQ_ASSERT(x) kassert(x)
#define WQ_DPRINTF(fmt, ...) kprintf("waitqueue: " fmt, ##__VA_ARGS__)

#define WQC_TABLESIZE 64 // must be power of 2
#define WQC_HASH(wchan) (((uintptr_t)(wchan) >> 4) & (WQC_TABLESIZE - 1))
#define WQC_LOOKUP(wchan) (&waitq_chains[WQC_HASH(wchan)])

struct waitqueue_chain {
  struct mtx lock; // chain spin lock
  LIST_HEAD(struct waitqueue) head;
  LIST_HEAD(struct waitqueue) free;
};
static struct waitqueue_chain waitq_chains[WQC_TABLESIZE];

static void waitq_static_init() {
  for (int i = 0; i < WQC_TABLESIZE; i++) {
    struct waitqueue_chain *chain = &waitq_chains[i];
    mtx_init(&chain->lock, MTX_SPIN, "waitqueue_chain_lock");
    LIST_INIT(&chain->head);
  }
}
STATIC_INIT(waitq_static_init);

// this version doesn't unlock the waitq and chainlock
void waitq_remove_internal(struct waitqueue *waitq, struct waitqueue_chain *chain, thread_t *td) {
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
  struct waitqueue_chain *chain = WQC_LOOKUP(wchan);
  mtx_spin_lock(&chain->lock);

  struct waitqueue *waitq = NULL;
  LIST_FOREACH(waitq, &chain->head, chain_list) {
    if (waitq->wchan == wchan) {
      mtx_spin_lock(&waitq->lock);
      break;
    }
  }
  return waitq;
}

struct waitqueue *waitq_lookup_or_default(int type, const void *wchan, struct waitqueue *default_waitq) {
  ASSERT(type == WQ_SLEEP || type == WQ_CONDV);
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

void waitq_add(struct waitqueue *waitq, const char *wdmsg) {
  struct waitqueue_chain *chain = WQC_LOOKUP(waitq->wchan);
  mtx_assert(&chain->lock, MA_OWNED);
  mtx_assert(&waitq->lock, MA_OWNED);

  thread_t *td = curthread;
  td_lock(td);
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

  mtx_spin_unlock(&waitq->lock);
  mtx_spin_unlock(&chain->lock);
  sched_again(SCHED_SLEEPING);
}

void waitq_remove(struct waitqueue *waitq, thread_t *td) {
  WQ_ASSERT(TDS_IS_WAITING(td));
  struct waitqueue_chain *chain = WQC_LOOKUP(waitq->wchan);
  mtx_assert(&chain->lock, MA_OWNED);
  mtx_assert(&waitq->lock, MA_OWNED);

  waitq_remove_internal(waitq, chain, td);
  mtx_spin_unlock(&waitq->lock);
  mtx_spin_unlock(&chain->lock);
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
