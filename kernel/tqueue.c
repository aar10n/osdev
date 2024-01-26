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

  mtx_lock(&runq->lock);
  LIST_ADD_FRONT(&runq->head, td, rqlist);
  atomic_fetch_add(&runq->count, 1);

  td->runq = runq;
  mtx_unlock(&runq->lock);
}

void runq_remove(struct runqueue *runq, thread_t *td, bool *empty) {
  size_t count;
  mtx_lock(&runq->lock);
  LIST_REMOVE(&runq->head, td, rqlist);
  count = atomic_fetch_sub(&runq->count, 1);

  td->runq = NULL;
  if (empty != NULL) {
    *empty = count == 0;
  }
  mtx_unlock(&runq->lock);
}

thread_t *runq_next_thread(struct runqueue *runq, bool *empty) {
  size_t count;
  mtx_lock(&runq->lock);
  thread_t *td = LIST_FIRST(&runq->head);
  if (td != NULL) {
    LIST_REMOVE(&runq->head, td, rqlist);
    count = atomic_fetch_sub(&runq->count, 1);
  }

  td->runq = NULL;
  if (empty != NULL) {
    *empty = count == 0;
  }
  mtx_unlock(&runq->lock);
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

static void lockq_static_init() {
  for (int i = 0; i < LQC_TABLESIZE; i++) {
    struct lockqueue_chain *chain = &lqc_table[i];
    mtx_init(&chain->lock, MTX_SPIN, "lockqueue_chain_lock");
  }
}
STATIC_INIT(lockq_static_init);


static void lockq_propagate_priorirty(struct lockqueue *lockq, thread_t *td) {
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
  struct lockqueue *lockq = *lockqp;
  mtx_destroy(&lockq->lock);
  kfree(lockq);
  *lockqp = NULL;
}

struct lockqueue *lockq_lookup(struct lock_object *lock_obj) {
  struct lockqueue_chain *chain = LQC_LOOKUP(lock_obj);
  mtx_assert(&chain->lock, MA_OWNED);

  struct lockqueue *lockq = NULL;
  LIST_FOREACH(lockq, &chain->head, chain_list) {
    if (lockq->lock_obj == lock_obj) {
      break;
    }
  }
  return lockq;
}

void lockq_chain_lock(struct lock_object *lock_obj) {
  struct lockqueue_chain *chain = LQC_LOOKUP(lock_obj);
  if (chain != NULL)
    mtx_lock(&chain->lock);
}

void lockq_chain_unlock(struct lock_object *lock_obj) {
  struct lockqueue_chain *chain = LQC_LOOKUP(lock_obj);
  if (chain != NULL)
    mtx_unlock(&chain->lock);
}

void lockq_wait(struct lockqueue *lockq, thread_t *owner, int queue) {
  LQ_ASSERT(queue == LQ_EXCL || queue == LQ_SHRD);
  struct lockqueue_chain *chain = LQC_LOOKUP(lockq->lock_obj);
  LQ_ASSERT(chain != NULL);
  mtx_assert(&chain->lock, MA_OWNED);

  thread_t *td = curthread;
  td_lock(td);
  if (lockq == td->own_lockq) {
    // there was no existing lockq for the lock so curthread has donated its own lockq to block on
    LIST_ADD(&chain->head, lockq, chain_list);
  } else {
    // we are queueing onto an existing lockq, give ours up to the free list
    LIST_ADD(&chain->free, td->own_lockq, chain_list);
  }
  td->own_lockq = NULL;

  mtx_spin_lock(&lockq->lock);
  LIST_ADD(&lockq->queues[queue], td, lqlist);
  mtx_spin_unlock(&lockq->lock);

  sched_again(SCHED_BLOCKED);

  mtx_spin_lock(&lockq->lock);
  LIST_REMOVE(&lockq->queues[queue], td, lqlist);
  mtx_spin_unlock(&lockq->lock);
}

void lockq_remove(struct lockqueue *lockq, thread_t *td, int queue) {
  LQ_ASSERT(queue == LQ_EXCL || queue == LQ_SHRD);
  struct lockqueue_chain *chain = LQC_LOOKUP(lockq->lock_obj);
  LQ_ASSERT(chain != NULL);
  mtx_assert(&chain->lock, MA_OWNED);

  mtx_spin_lock(&lockq->lock);
  LIST_REMOVE(&lockq->queues[queue], td, lqlist);
  mtx_spin_unlock(&lockq->lock);
}

void lockq_signal(struct lockqueue *lockq, int queue) {
  LQ_ASSERT(queue == LQ_EXCL || queue == LQ_SHRD);
  mtx_assert(&lockq->lock, MA_OWNED);

  LIST_REMOVE(&lockq->queues[queue], curthread, lqlist);

}

void lockq_broadcast(struct lockqueue *lockq, int queue) {
  todo();
}

// =================================
//            waitqueue
// =================================

#define WQC_TABLESIZE 64 // must be power of 2
#define WQC_HASH(wchan) (((uintptr_t)(wchan) >> 4) & (WQC_TABLESIZE - 1))
#define WQC_LOOKUP(wchan) (&waitq_chains[WQC_HASH(wchan)])

struct waitqueue_chain {
  struct mtx lock; // chain spin lock
  LIST_HEAD(struct waitqueue) head;
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

//

struct waitqueue *waitq_alloc() {
  struct waitqueue *waitq = kmallocz(sizeof(struct waitqueue));
  mtx_init(&waitq->lock, MTX_SPIN, "waitqueue_lock");
  return waitq;
}

void waitq_free(struct waitqueue **waitqp) {
  struct waitqueue *waitq = *waitqp;
  mtx_destroy(&waitq->lock);
  kfree(waitq);
  *waitqp = NULL;
}

struct waitqueue *waitq_lookup(const void *wchan) {
  struct waitqueue_chain *chain = WQC_LOOKUP(wchan);
  mtx_assert(&chain->lock, MA_OWNED);

  struct waitqueue *waitq = NULL;
  LIST_FOREACH(waitq, &chain->head, chain_list) {
    if (waitq->wchan == wchan) {
      break;
    }
  }
  return waitq;
}

void waitq_chain_lock(const void *wchan) {
  struct waitqueue_chain *chain = WQC_LOOKUP(wchan);
  if (chain != NULL)
    mtx_spin_lock(&chain->lock);
}

void waitq_chain_unlock(const void *wchan) {
  struct waitqueue_chain *chain = WQC_LOOKUP(wchan);
  if (chain != NULL)
    mtx_spin_unlock(&chain->lock);
}

void waitq_wait(struct waitqueue *waitq, const char *wdmsg) {

}

void waitq_remove(struct waitqueue *waitq, thread_t *td) {

}
