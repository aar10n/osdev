//
// Created by Aaron Gill-Braun on 2023-12-28.
//

#ifndef KERNEL_COND_H
#define KERNEL_COND_H

#include <kernel/lock.h>
#include <kernel/mutex.h>

// =================================
//             condvar
// =================================

/*
 * Condition variable.
 *
 * A condition variable is a synchronization primitive that is used in conjunction
 * with a mutex to wait for a particular condition to become true.
 */
typedef struct cond {
  const char *name;  // name for debugging
  int waiters;       // number of waiting threads
} cond_t;

void cond_init(cond_t *cond, const char *name);
void cond_destroy(cond_t *cond);

void cond_wait(cond_t *cond, mtx_t *lock);
// int cond_timedwait(cond_t *cond, mtx_t *lock, uint64_t timeout);
void cond_signal(cond_t *cond);
void cond_broadcast(cond_t *cond);

#endif
