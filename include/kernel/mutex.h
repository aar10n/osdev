//
// Created by Aaron Gill-Braun on 2021-03-23.
//

#ifndef KERNEL_MUTEX_H
#define KERNEL_MUTEX_H

#include <base.h>
#include <spinlock.h>
#include <queue.h>

typedef struct thread thread_t;

typedef LIST_HEAD(thread_t) tqueue_t;

// -------- Mutexes --------

#define MUTEX_LOCKED  0x1   // mutex locked initially
#define MUTEX_REENTRANT 0x2 // mutex is reentrant
#define MUTEX_SHARED 0x4    // mutex can be shared between processes

typedef struct mutex {
  volatile uint32_t flags; // flags
  tqueue_t queue;          // queue
  thread_t *aquired_by;    // owning thread
  uint8_t aquire_count;    // reentrant count
} mutex_t;

void mutex_init(mutex_t *mutex, uint32_t flags);
int mutex_lock(mutex_t *mutex);
int mutex_unlock(mutex_t *mutex);
int mutex_trylock(mutex_t *mutex);

// -------- Conditions --------

#define COND_SIGNALED 0x1 // condition is signaled
#define COND_NOEMPTY  0x2 // signal doesnt work if queue is empty

typedef struct cond {
  volatile uint32_t flags; // flags
  tqueue_t queue;          // queue
} cond_t;

void cond_init(cond_t *cond, uint32_t flags);
int cond_wait(cond_t *cond);
int cond_wait_timeout(cond_t *cond, uint64_t us);
int cond_signal(cond_t *cond);
int cond_broadcast(cond_t *cond);
int cond_signaled(cond_t *cond);
int cond_clear_signal(cond_t *cond);

// -------- Read/Write Locks --------

typedef struct rw_lock {
  mutex_t mutex;
  cond_t cond;
  int64_t readers;
} rw_lock_t;

void rw_lock_init(rw_lock_t *lock);
int rw_lock_read(rw_lock_t *lock);
int rw_lock_write(rw_lock_t *lock);
int rw_unlock_read(rw_lock_t *lock);
int rw_unlock_write(rw_lock_t *lock);

#endif
