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

void mutex_init(mutex_t *mutex) {
  mutex->locked = false;
  mutex->owner = NULL;
  mutex->queue = NULL;
  spin_init(&mutex->queue_lock);
}

void mutex_init_locked(mutex_t *mutex, thread_t *owner) {
  mutex->locked = true;
  mutex->owner = owner;
  mutex->queue = NULL;
  spin_init(&mutex->queue_lock);
}


int mutex_lock(mutex_t *mutex) {
  thread_t *thread = current_thread;
  if (mutex->owner == thread) {
    return EINVAL;
  }

  kprintf("[pid %d:%d] locking mutex\n", getpid(), gettid());
  preempt_disable();
  // label(try_again);
  if (atomic_bit_test_and_set(&mutex->locked)) {
    kprintf("[pid %d:%d] failed to aquire mutex\n", getpid(), gettid());
    kprintf("blocking\n");
    // the mutex is currently locked
    safe_enqeue(&mutex->queue, &mutex->queue_lock, thread);
    scheduler_block(thread);
    // goto try_again;
    kassert(mutex->owner == thread);
  } else {
    mutex->owner = thread;
  }
  preempt_enable();
  kprintf("[pid %d:%d] mutex aquired\n", getpid(), gettid());
  return 0;
}

int mutex_unlock(mutex_t *mutex) {
  thread_t *thread = current_thread;
  if (mutex->owner != thread || !mutex->locked) {
    return EINVAL;
  }

  kprintf("[pid %d:%d] unlocking mutex\n", getpid(), gettid());
  preempt_disable();
  thread_t *unblocked = safe_dequeue(&mutex->queue, &mutex->queue_lock);
  if (unblocked != NULL) {
    mutex->owner = unblocked;
    scheduler_unblock(unblocked);
  }
  mutex->owner = NULL;
  // atomic_bit_test_and_reset(&mutex->locked);
  preempt_enable();
  kprintf("[pid %d:%d] mutex unlocked\n", getpid(), gettid());
  return 0;
}

// Conditions

void cond_init(cond_t *cond) {
  cond->signaled = false;
  cond->signaler = NULL;
  cond->queue = NULL;
  spin_init(&cond->queue_lock);
}

int cond_wait(cond_t *cond) {
  thread_t *thread = current_thread;
  if (cond->signaled) {
    atomic_bit_test_and_reset(&cond->signaled);
    return 0;
  }

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

  kprintf("[cond] thread %d:%d unblocked by %d:%d\n",
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
