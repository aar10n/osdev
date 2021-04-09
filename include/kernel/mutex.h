//
// Created by Aaron Gill-Braun on 2021-03-23.
//

#ifndef KERNEL_MUTEX_H
#define KERNEL_MUTEX_H

#include <base.h>
#include <spinlock.h>

typedef struct thread thread_t;

typedef struct thread_link {
  thread_t *thread;
  struct thread_link *next;
} thread_link_t;

// -------- Mutexes --------

typedef struct mutex {
  bool locked;           // mutex lock
  uint32_t flags;        // mutex flags
  thread_t *owner;       // mutex owner
  spinlock_t queue_lock; // thread queue spinlock
  thread_link_t *queue;  // thread queue
} mutex_t;

// -------- Conditions --------

typedef struct cond {
  bool signaled;         // signaled flag
  uint32_t flags;        // condition flags
  thread_t *signaler;    // signalling thread
  spinlock_t queue_lock; // queue lock
  thread_link_t *queue;  // signaled threads
} cond_t;


void mutex_init(mutex_t *mutex, uint32_t flags);
void mutex_init_locked(mutex_t *mutex, thread_t *owner, uint32_t flags);
int mutex_lock(mutex_t *mutex);
int mutex_unlock(mutex_t *mutex);

void cond_init(cond_t *cond, uint32_t flags);
int cond_wait(cond_t *cond);
int cond_signal(cond_t *cond);
int cond_broadcast(cond_t *cond);


#endif
