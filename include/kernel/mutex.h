//
// Created by Aaron Gill-Braun on 2021-03-23.
//

#ifndef KERNEL_MUTEX_H
#define KERNEL_MUTEX_H

#include <base.h>
#include <spinlock.h>

typedef struct thread thread_t;

typedef struct tqueue {
  thread_t *head; // queue head
  thread_t *tail; // queue tail
} tqueue_t;


// -------- Mutexes --------

#define MUTEX_LOCKED  0x1 // mutex lock

typedef struct mutex {
  volatile uint32_t flags; // flags
  tqueue_t queue;          // queue
} mutex_t;

void mutex_init(mutex_t *mutex, uint32_t flags);
int mutex_lock(mutex_t *mutex);
int mutex_unlock(mutex_t *mutex);

// -------- Conditions --------

#define COND_SIGNALED 0x1 // condition is signaled
#define COND_NOEMPTY  0x2 // signal doesnt work if queue is empty

typedef struct cond {
  volatile uint32_t flags;  // flags
  tqueue_t queue;           // queue
} cond_t;

void cond_init(cond_t *cond, uint32_t flags);
int cond_wait(cond_t *cond);
int cond_wait_timeout(cond_t *cond, uint64_t us);
int cond_signal(cond_t *cond);
int cond_broadcast(cond_t *cond);

#endif
