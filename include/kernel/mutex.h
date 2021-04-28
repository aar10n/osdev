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

// flags
#define MUTEX_LOCKED  0x1 // mutex lock

// -------- Mutexes --------

typedef struct mutex {
  volatile uint32_t flags; // flags
  tqueue_t queue;          // queue
} mutex_t;

void mutex_init(mutex_t *mutex, uint32_t flags);
int mutex_lock(mutex_t *mutex);
int mutex_unlock(mutex_t *mutex);

// -------- Conditions --------

typedef struct cond {
  uint32_t flags;  // flags
  tqueue_t queue;  // queue
} cond_t;

void cond_init(cond_t *cond, uint32_t flags);
int cond_wait(cond_t *cond);
int cond_signal(cond_t *cond);
int cond_broadcast(cond_t *cond);

#endif
