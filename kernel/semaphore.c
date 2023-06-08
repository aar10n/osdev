//
// Created by Aaron Gill-Braun on 2021-07-09.
//

#include <semaphore.h>
#include <atomic.h>
#include <panic.h>


void semaphore_init(semaphore_t *sem, int max_value) {
  kassert(max_value > 0);
  spin_init(&sem->lock);
  cond_init(&sem->wait, 0);
  sem->value = max_value;
}

int semaphore_aquire(semaphore_t *sem) {
  int value;
  while ((value = atomic_fetch_sub(&sem->value, 1)) <= 0) {
    cond_wait(&sem->wait);
  }
  return 0;
}

int semaphore_release(semaphore_t *sem) {
  int value = atomic_fetch_add(&sem->value, 1);
  if (value <= 0)
    cond_signal(&sem->wait);
  return 0;
}


