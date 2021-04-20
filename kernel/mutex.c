//
// Created by Aaron Gill-Braun on 2021-03-23.
//

#include <mutex.h>
#include <spinlock.h>
#include <scheduler.h>
#include <thread.h>
#include <atomic.h>
#include <mm.h>
#include <printf.h>
#include <panic.h>

// #define MUTEX_DEBUG
#ifdef MUTEX_DEBUG
#define mutex_trace_debug(str, args...) kprintf("[mutex] " str "\n", ##args)
#else
#define mutex_trace_debug(str, args...)
#endif
// #define COND_DEBUG
#ifdef COND_DEBUG
#define cond_trace_debug(str, args...) kprintf("[cond] " str "\n", ##args)
#else
#define cond_trace_debug(str, args...)
#endif


void safe_enqeue(thread_link_t **queue, spinlock_t *lock, thread_t *thread) {
  thread_link_t *link = kmalloc(sizeof(thread_link_t));
  link->thread = thread;

  spin_lock(lock);
  link->next = *queue;
  *queue = link;
  spin_unlock(lock);
}

thread_t *safe_dequeue(thread_link_t **queue, spinlock_t *lock) {
  if (*queue == NULL) {
    return NULL;
  }

  spin_lock(lock);
  thread_link_t *link = *queue;
  *queue = link->next;
  spin_unlock(lock);

  thread_t *thread = link->thread;
  kfree(link);
  return thread;
}

// Mutexes

void mutex_init(mutex_t *mutex, uint32_t flags) {
  mutex->locked = false;
  mutex->flags = flags;
  mutex->owner = NULL;
  mutex->queue = NULL;
  spin_init(&mutex->queue_lock);
}

void mutex_init_locked(mutex_t *mutex, thread_t *owner, uint32_t flags) {
  mutex->locked = true;
  mutex->flags = flags;
  mutex->owner = owner;
  mutex->queue = NULL;
  spin_init(&mutex->queue_lock);
}


int mutex_lock(mutex_t *mutex) {
  thread_t *thread = current_thread;
  if (mutex->owner == thread) {
    return EINVAL;
  }

  mutex_trace_debug("locking mutex (%d:%d)", getpid(), gettid());
  preempt_disable();
  // label(try_again);
  if (atomic_bit_test_and_set(&mutex->locked)) {
    mutex_trace_debug("failed to aquire mutex (%d:%d)", getpid(), gettid());
    mutex_trace_debug("blocking");
    // the mutex is currently locked
    safe_enqeue(&mutex->queue, &mutex->queue_lock, thread);
    scheduler_block(thread);
    // goto try_again;
    kassert(mutex->owner == thread);
  } else {
    mutex->owner = thread;
  }
  preempt_enable();
  mutex_trace_debug("mutex aquired (%d:%d)", getpid(), gettid());
  return 0;
}

int mutex_unlock(mutex_t *mutex) {
  thread_t *thread = current_thread;
  if (mutex->owner != thread || !mutex->locked) {
    return EINVAL;
  }

  mutex_trace_debug("unlocking mutex (%d:%d)", getpid(), gettid());
  preempt_disable();
  thread_t *unblocked = safe_dequeue(&mutex->queue, &mutex->queue_lock);
  if (unblocked != NULL) {
    mutex->owner = unblocked;
    scheduler_unblock(unblocked);
  }
  mutex->owner = NULL;
  // atomic_bit_test_and_reset(&mutex->locked);
  preempt_enable();
  mutex_trace_debug("mutex unlocked (%d:%d)", getpid(), gettid());
  return 0;
}

// Conditions

void cond_init(cond_t *cond, uint32_t flags) {
  cond->signaled = false;
  cond->flags = flags;
  cond->signaler = NULL;
  cond->queue = NULL;
  spin_init(&cond->queue_lock);
}

int cond_wait(cond_t *cond) {
  thread_t *thread = current_thread;
  if (cond->signaled) {
    cond->signaled = false;
    return 0;
  }

  cond_trace_debug("thread %d:%d blocked by condition",
                   thread->process->pid, thread->tid);

  preempt_disable();
  safe_enqeue(&cond->queue, &cond->queue_lock, thread);
  preempt_enable();
  scheduler_block(thread);
  return 0;
}

int cond_signal(cond_t *cond) {
  thread_t *thread = current_thread;
  if (cond->queue == NULL) {
    cond->signaled = true;
    return 0;
  }

  preempt_disable();
  cond->signaler = thread;
  thread_t *signaled = safe_dequeue(&cond->queue, &cond->queue_lock);
  preempt_enable();

  cond_trace_debug("thread %d:%d unblocked by %d:%d",
          signaled->process->pid, signaled->tid,
          thread->process->pid, thread->tid);

  scheduler_unblock(signaled);
  return 0;
}

int cond_broadcast(cond_t *cond) {
  thread_t *thread = current_thread;
  if (cond->queue == NULL) {
    cond->signaled = true;
    return 0;
  }

  preempt_disable();
  cond->signaler = thread;

  thread_t *signaled;
  while ((signaled = safe_dequeue(&cond->queue, &cond->queue_lock))) {
    scheduler_unblock(signaled);
  }
  preempt_enable();
  return 0;
}
