//
// Created by Aaron Gill-Braun on 2021-07-09.
//

#include <semaphore.h>
#include <panic.h>


void semaphore_init(semaphore_t *sem, int64_t max_value) {
  kassert(max_value > 0);

  spin_init(&sem->lock);
  cond_init(&sem->wait, COND_NOEMPTY);
  sem->value = max_value;
}

int semaphore_aquire(semaphore_t *sem) {
  spin_lock(&sem->lock);
  sem->value--;
  spin_unlock(&sem->lock);

  if (sem->value < 0) {
    cond_wait(&sem->wait);
  }
  return 0;
}

int semaphore_release(semaphore_t *sem) {
  spin_lock(&sem->lock);
  sem->value++;
  spin_unlock(&sem->lock);

  if (sem->value > 0) {
    cond_signal(&sem->wait);
  }
  return 0;
}


