//
// Created by Aaron Gill-Braun on 2021-03-23.
//

#include <kernel/mutex.h>

#include <kernel/cpu/cpu.h>

#include <kernel/mm.h>
#include <kernel/sched.h>
#include <kernel/process.h>
#include <kernel/thread.h>
#include <kernel/timer.h>

#include <atomic.h>
#include <kernel/panic.h>
#include <kernel/printf.h>

// #define MUTEX_DEBUG
#ifdef MUTEX_DEBUG
#define mutex_trace_debug(str, args...) kprintf("mutex: " str "\n", ##args)
#else
#define mutex_trace_debug(str, args...)
#endif
// #define COND_DEBUG
#ifdef COND_DEBUG
#define cond_trace_debug(str, args...) kprintf("[CPU#%d] cond: " str "\n", PERCPU_ID, ##args)
#else
#define cond_trace_debug(str, args...)
#endif
// #define SHARED_MUTEX_DEBUG
#ifdef SHARED_MUTEX_DEBUG
#define shd_mutex_trace_debug(str, args...) kprintf("shrd_mutex: " str "\n", ##args)
#else
#define shd_mutex_trace_debug(str, args...)
#endif

// flag bits
#define B_LOCKED        0 // mutex lock bit
#define B_TIMEOUT      30 // condition timed out
#define B_QUEUE_LOCKED 31 // mutex queue lock bit
// flag masks
#define M_LOCKED       (1 << B_LOCKED)
#define M_TIMEOUT      (1 << B_TIMEOUT)
#define M_QUEUE_LOCKED (1 << B_QUEUE_LOCKED)

static inline uint64_t inline_lock(volatile uint32_t *flags) {
  uint64_t rflags = cpu_save_clear_interrupts();
  if (atomic_bit_test_and_set(flags, B_QUEUE_LOCKED)) {
    while (atomic_bit_test_and_set(flags, B_QUEUE_LOCKED)) {
      cpu_pause(); // spin
    }
  }
  return rflags;
}

static inline void inline_unlock(volatile uint32_t *flags, uint64_t rflags) {
  atomic_bit_test_and_reset(flags, B_QUEUE_LOCKED);
  cpu_restore_interrupts(rflags);
}

void safe_enqeue(volatile uint32_t *flags, tqueue_t *queue, thread_t *thread) {
  uint64_t rflags = inline_lock(flags);
  LIST_ADD_FRONT(queue, thread, list);
  inline_unlock(flags, rflags);
}

thread_t *safe_dequeue(volatile uint32_t *flags, tqueue_t *queue) {
  uint64_t rflags = inline_lock(flags);
  thread_t *thread = LIST_LAST(queue);
  if (thread != NULL) {
    LIST_REMOVE(queue, thread, list);
  }
  inline_unlock(flags, rflags);
  return thread;
}

// Mutexes

void mutex_init(mutex_t *mutex, uint32_t flags) {
  mutex->flags = flags;
  LIST_INIT(&mutex->queue);
  mutex->aquired_by = NULL;
  mutex->aquire_count = 0;
}

int mutex_lock(mutex_t *mutex) {
  thread_t *thread = PERCPU_THREAD;
  if (!(mutex->flags & MUTEX_SHARED) && (mutex->aquired_by && thread->process != mutex->aquired_by->process)) {
    panic("mutex belongs to another process");
  }

  mutex_trace_debug("locking mutex (%d:%d)", process_getpid(), process_gettid());
  // thread->preempt_count++;
  if (atomic_bit_test_and_set(&mutex->flags, B_LOCKED)) {
    if (mutex->flags & MUTEX_REENTRANT && mutex->aquired_by == thread) {
      if (mutex->aquire_count == UINT8_MAX - 1) {
        panic("reentrancy too deep");
      }
      goto done;
    }

    // mutex is already locked
    mutex_trace_debug("failed to aquire mutex (%d:%d)", process_getpid(), process_gettid());
    mutex_trace_debug("blocking");

    safe_enqeue(&mutex->flags, &mutex->queue, thread);
    sched_block(thread);
  }
done:;
  mutex->aquired_by = thread;
  mutex->aquire_count++;
  // thread->preempt_count--;
  mutex_trace_debug("mutex aquired (%d:%d)", process_getpid(), process_gettid());
  return 0;
}

int mutex_unlock(mutex_t *mutex) {
  thread_t *thread = PERCPU_THREAD;
  if (!(mutex->flags & MUTEX_LOCKED)) {
    return -EINVAL;
  }
  kassert(mutex->aquired_by == PERCPU_THREAD);

  mutex_trace_debug("unlocking mutex (%d:%d)", process_getpid(), process_gettid());
  thread->preempt_count++;
  if (mutex->flags & MUTEX_REENTRANT) {
    kassert(mutex->aquire_count > 0);
    mutex->aquire_count--;
    if (mutex->aquire_count > 0) {
      thread->preempt_count--;
      return 0;
    }
  }

  thread_t *next = safe_dequeue(&mutex->flags, &mutex->queue);
  atomic_bit_test_and_reset(&mutex->flags, B_LOCKED);
  if (mutex->flags & MUTEX_SHARED) {
    mutex->aquired_by = NULL;
  } else {
    mutex->aquired_by = thread->process->main;
  }
  if (next != NULL) {
    sched_unblock(next);
  }
  thread->preempt_count--;
  mutex_trace_debug("mutex unlocked (%d:%d)", process_getpid(), process_gettid());
  return 0;
}

int mutex_trylock(mutex_t *mutex) {
  thread_t *thread = PERCPU_THREAD;
  if (!(mutex->flags & MUTEX_SHARED) && thread->process != mutex->aquired_by->process) {
    panic("mutex belongs to another process");
  }

  mutex_trace_debug("trying to lock mutex (%d:%d)", process_getpid(), process_gettid());
  // thread->preempt_count++;
  if (atomic_bit_test_and_set(&mutex->flags, B_LOCKED)) {
    if (mutex->flags & MUTEX_REENTRANT && mutex->aquired_by == thread) {
      if (mutex->aquire_count == UINT8_MAX - 1) {
        panic("reentrancy too deep");
      }
      goto done;
    }

    // mutex is already locked
    return -1;
  }
done:;
  mutex->aquired_by = thread;
  mutex->aquire_count++;
  // thread->preempt_count--;
  mutex_trace_debug("mutex aquired (%d:%d)", process_getpid(), process_gettid());
  return 0;
}

// Conditions

static void cond_timeout_cb(void *arg) {
  cond_t *cond = arg;
  cond->flags |= M_TIMEOUT;
  cond_signal(cond);
}

//

void cond_init(cond_t *cond, uint32_t flags) {
  cond->flags = flags;
  LIST_INIT(&cond->queue);
}

int cond_wait(cond_t *cond) {
  thread_t *thread = PERCPU_THREAD;

  if (cond->flags & M_LOCKED) {
    cond->flags ^= M_LOCKED;
    return 0;
  }

  cond_trace_debug("thread %d:%d [%s] blocked by condition %p",
                   thread->process->pid, thread->tid, thread->name, cond);

  thread->flags |= F_THREAD_OWN_BLOCKQ;
  safe_enqeue(&cond->flags, &cond->queue, thread);
  sched_block(thread);
  return 0;
}

int cond_wait_timeout(cond_t *cond, uint64_t us) {
  if (cond->flags & M_LOCKED) {
    cond->flags ^= M_LOCKED;
    return 0;
  }

  uint64_t timeout_ns = us * (NS_PER_SEC / US_PER_SEC);
  clockid_t id = timer_create_alarm(timer_now() + timeout_ns, cond_timeout_cb, cond);
  if (id < 0) {
    return id;
  }

  cond_wait(cond);
  timer_delete_alarm(id);
  if (cond->flags & M_TIMEOUT){
    cond->flags ^= M_TIMEOUT;
    return 1;
  }
  return 0;
}

int cond_signal(cond_t *cond) {
  if (LIST_LAST(&cond->queue) == NULL) {
    if (!(cond->flags & COND_NOEMPTY)) {
      cond->flags |= M_LOCKED;
    }
    return 0;
  }

  thread_t *thread = PERCPU_THREAD;
  thread_t *signaled = safe_dequeue(&cond->flags, &cond->queue);
  cond_trace_debug("thread %d:%d [%s] unblocked by %d:%d [%s] %p",
                   signaled->process->pid, signaled->tid, signaled->name,
                   thread->process->pid, thread->tid, thread->name, cond);

  sched_unblock(signaled);
  return 0;
}

int cond_broadcast(cond_t *cond) {
  thread_t *thread = PERCPU_THREAD;
  if (LIST_LAST(&cond->queue) == NULL) {
    cond->flags |= M_LOCKED;
    return 0;
  }

  thread_t *signaled;
  while ((signaled = safe_dequeue(&cond->flags, &cond->queue))) {
    sched_unblock(signaled);
  }
  return 0;
}

int cond_signaled(cond_t *cond) {
  return cond->flags & M_LOCKED;
}

int cond_clear_signal(cond_t *cond) {
  cond->flags &= ~M_LOCKED;
  return 0;
}

// Read/Write Locks

void rw_lock_init(rw_lock_t *lock) {
  mutex_init(&lock->mutex, 0);
  cond_init(&lock->cond, COND_NOEMPTY);
  lock->readers = 0;
}

int rw_lock_read(rw_lock_t *lock) {
  mutex_lock(&lock->mutex);
  lock->readers++;
  mutex_unlock(&lock->mutex);
  return 0;
}

int rw_lock_write(rw_lock_t *lock) {
  mutex_lock(&lock->mutex);
  for (int64_t i = 0; i < lock->readers; i++) {
    cond_wait(&lock->cond);
  }
  return 0;
}

int rw_unlock_read(rw_lock_t *lock) {
  mutex_lock(&lock->mutex);
  lock->readers--;
  if (lock->readers == 0) {
    cond_signal(&lock->cond);
  }
  mutex_unlock(&lock->mutex);
  return 0;
}

int rw_unlock_write(rw_lock_t *lock) {
  mutex_unlock(&lock->mutex);
  return 0;
}
