//
// Created by Aaron Gill-Braun on 2021-07-09.
//

#ifndef KERNEL_SEMAPHORE_H
#define KERNEL_SEMAPHORE_H

#include <base.h>
#include <mutex.h>
#include <spinlock.h>

// -------- Semaphores --------

typedef struct semaphore {
  spinlock_t lock;
  cond_t wait;
  int64_t value;
} semaphore_t;


void semaphore_init(semaphore_t *sem, int64_t max_count);
int semaphore_aquire(semaphore_t *sem);
int semaphore_release(semaphore_t *sem);

#endif
