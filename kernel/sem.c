//
// Created by Aaron Gill-Braun on 2024-11-13.
//

#include <kernel/sem.h>

void _sem_init(sem_t *sem, int value, uint32_t opts, const char *name) {
  if (opts & SEM_SPIN) {
    _mtx_init(&sem->lock, MTX_SPIN | (opts & SEM_DEBUG), name);
  } else {
    _mtx_init(&sem->lock, (opts & SEM_DEBUG), name);
  }
  atomic_store(&sem->count, value);

  sem->name = name;
}

void _sem_destroy(sem_t *sem) {

  _mtx_destroy(&sem->lock);
}

int _sem_try_down(sem_t *sem, const char *file, int line) {
  int count = atomic_load(&sem->count);
  while (count > 0) {
    if (atomic_cmpxchg(&sem->count, count, count - 1)) {
      return 1;
    }
    count = atomic_load(&sem->count);
  }
  return 0;
}

void _sem_down(sem_t *sem, const char *file, int line) {
  // Fast path: try atomic decrement first
  int count = atomic_load(&sem->count);
  while (count > 0) {
    if (atomic_cmpxchg(&sem->count, count, count - 1)) {
      return;
    }
    count = atomic_load(&sem->count);
  }

  // Need to block - acquire lock and add to wait queue
  _mtx_spin_lock(&sem->lock, file, line);

  // Recheck count after acquiring lock
  count = atomic_load(&sem->count);
  if (count > 0) {
    atomic_fetch_sub(&sem->count, 1);
    _mtx_spin_unlock(&sem->lock, file, line);
    return;
  }

  // Lock the waitqueue chain and add current thread
  // waitq_chain_lock(sem);

  _mtx_spin_unlock(&sem->lock, file, line);  // Release sem lock after chain lock acquired

  // Wait on the queue - this will context switch
  // waitq_wait(sem->waitq, "semaphore down");
  // waitq_wait returns with chain lock released
}

void _sem_up(sem_t *sem, const char *file, int line) {
  _mtx_spin_lock(&sem->lock, file, line);

  // // First check if there are any waiters
  // waitq_chain_lock(sem);
  // struct thread *td = LIST_FIRST(&sem->waitq->queue);
  // if (td != NULL) {
  //   // Remove thread from waitqueue and wake it
  //   waitq_remove(sem->waitq, td);
  //   waitq_chain_unlock(sem);
  //   _mtx_spin_unlock(&sem->lock, file, line);
  //   return;
  // }

  // No waiters, just increment count
  atomic_fetch_add(&sem->count, 1);
  _mtx_spin_unlock(&sem->lock, file, line);
}
