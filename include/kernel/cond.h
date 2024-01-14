//
// Created by Aaron Gill-Braun on 2023-12-28.
//

#ifndef KERNEL_COND_H
#define KERNEL_COND_H

#include <kernel/lock.h>

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
  const char *description;
  int waiters;
} cond_t;

void cond_init(cond_t *cond, const char *desc);
void cond_destroy(cond_t *cond);

void cond_wait(cond_t *cond, struct lock_object *lock);
int cond_wait_sig(cond_t *cond, struct lock_object *lock);
int cond_timedwait(cond_t *cond, struct lock_object *lock, uint64_t timeout);
// int cond_timedwait_sig(cond_t *cond, )

#endif
