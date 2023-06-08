//
// Created by Aaron Gill-Braun on 2021-07-09.
//

#ifndef KERNEL_SEMAPHORE_H
#define KERNEL_SEMAPHORE_H

#include <base.h>
#include <mutex.h>

// -------- Semaphores --------

typedef struct semaphore {
  cond_t wait;
  int value;
} semaphore_t;

void semaphore_init(semaphore_t *sem, int max_count);
int semaphore_aquire(semaphore_t *sem);
int semaphore_release(semaphore_t *sem);

#endif
