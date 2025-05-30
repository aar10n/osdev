//
// Created by Aaron Gill-Braun on 2024-11-13.
//

#ifndef KERNEL_SEM_H
#define KERNEL_SEM_H

#include <kernel/mutex.h>
#include <kernel/atomic.h>
#include <kernel/tqueue.h>

/*
 * Semaphore synchronization primitive.
 *
 * A semaphore maintains a count that can be incremented by up() and
 * decremented by down(). If the count would go below zero, the thread
 * will block until another thread calls up().
 */
typedef struct sem {
  volatile int count;           // current semaphore count
  mtx_t lock;                   // semaphore lock
} sem_t;

// semaphore init options
#define SEM_SPIN     0x1  // use spin mutex instead of wait mutex
#define SEM_DEBUG    0x2  // enable debugging for this semaphore

void _sem_init(sem_t *sem, int value, uint32_t opts, const char *name);
void _sem_destroy(sem_t *sem);
int _sem_try_down(sem_t *sem, const char *file, int line);
void _sem_down(sem_t *sem, const char *file, int line);
void _sem_up(sem_t *sem, const char *file, int line);

// Public API macros

#define sem_init(s, v, o, n) _sem_init(s, v, o, n)
#define sem_destroy(s) _sem_destroy(s)
/*
 * Try to decrement the semaphore count without blocking.
 * Returns 1 if successful, 0 if would block.
 */
#define sem_try_down(s) _sem_try_down(s, __FILE__, __LINE__)
/*
 * Decrement the semaphore count, blocking if it would go below zero.
 */
#define sem_down(s) _sem_down(s, __FILE__, __LINE__)
/*
 * Increment the semaphore count and wake one waiting thread if any.
 */
#define sem_up(s) _sem_up(s, __FILE__, __LINE__)

#endif
