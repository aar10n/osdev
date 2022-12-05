//
// Created by Aaron Gill-Braun on 2022-07-24.
//

#include <sched/sched.h>
#include <sched/fprr.h>

#include <cpu/cpu.h>
#include <cpu/io.h>

#include <mm.h>
#include <thread.h>
#include <process.h>
#include <clock.h>
#include <timer.h>
#include <ipi.h>

#include <printf.h>
#include <panic.h>
#include <atomic.h>

sched_policy_impl_t *policy_impl[NUM_POLICIES];
sched_t *_schedulers[MAX_CPUS] = {};
size_t _num_schedulers = 0;

// #define SCHED_UNIPROC

#define DPRINTF(...)
// #define DPRINTF(...) kprintf(__VA_ARGS__)

#define QDEBUG_VALUE(v) ({ outdw(0x800, v); })

#define QDEBUG_PRINT(str)
// #define QDEBUG_PRINT(str) \
//   ({                      \
//     const char *_ptr = str; \
//     while (*_ptr) {       \
//       outb(0x810 + PERCPU_ID, *_ptr); \
//       _ptr++; \
//     }                     \
//     outb(0x810 + PERCPU_ID, '\0'); \
//   })

#define QDEBUG_LOCK(d1, d2, c0, c1, c2, c3) \
  ({                                      \
    outdw(0x808, SIGNATURE_32(c0, c1, c2, c3)); \
    outdw(0x804, SIGNATURE_32(PERCPU_ID, 1, ((d1) & 0xFF), ((d2) & 0xFF))); \
  })
#define QDEBUG_UNLOCK(d1, d2, c0, c1, c2, c3) \
  ({                                        \
    outdw(0x808, SIGNATURE_32(c0, c1, c2, c3)); \
    outdw(0x804, SIGNATURE_32(PERCPU_ID, 0, ((d1) & 0xFF), ((d2) & 0xFF))); \
  })

#define LOCK(obj) (spin_lock(&(obj)->lock))
#define UNLOCK(obj) (spin_unlock(&(obj)->lock))

#define LOCK_SCHED(sched) LOCK(sched)
// #define LOCK_SCHED(sched) ({ QDEBUG_LOCK((sched)->cpu_id, (sched)->lock.lock_count, 's', 'c', 'e', 'd'); LOCK(sched); })
#define UNLOCK_SCHED(sched) UNLOCK(sched)
// #define UNLOCK_SCHED(sched) ({ QDEBUG_UNLOCK((sched)->cpu_id, (sched)->lock.lock_count, 's', 'c', 'e', 'd'); UNLOCK(sched); })
#define LOCK_THREAD(thread) LOCK(thread)
// #define LOCK_THREAD(thread) ({ QDEBUG_LOCK((thread)->tid, (thread)->lock.lock_count, 't', 'h', 'r', 'd'); LOCK(thread); })
#define UNLOCK_THREAD(thread) UNLOCK(thread)
// #define UNLOCK_THREAD(thread) ({ QDEBUG_UNLOCK((thread)->tid, (thread)->lock.lock_count, 't', 'h', 'r', 'd'); UNLOCK(thread); })
#define LOCK_POLICY(sched, thread) LOCK(POLICY_T(sched, thread))
// #define LOCK_POLICY(sched, thread) ({ QDEBUG_LOCK((sched)->cpu_id, 0, 'p', 'o', 'l', 'i'); LOCK(POLICY_T(sched, thread)); })
#define UNLOCK_POLICY(sched, thread) UNLOCK(POLICY_T(sched, thread))
// #define UNLOCK_POLICY(sched, thread) ({ QDEBUG_UNLOCK((sched)->cpu_id, 0, 'p', 'o', 'l', 'i'); UNLOCK(POLICY_T(sched, thread)); })

#define sched_assert(expr) kassert(expr)
// #define sched_assert(expr)
#define thread_assert(thread, expr) \
  kassertf(expr, #expr ", thread %d.%d [%s] [CPU#%d]", (thread)->process->pid, (thread)->tid, (thread)->name, PERCPU_ID)


#define foreach_sched(var) \
  sched_t * var = NULL; int i = 0; \
  for (i = 0, var = _schedulers[i]; i < _num_schedulers; i++, var = _schedulers[i])

#define foreach_policy(var) \
  for (uint16_t var = 0; var < NUM_POLICIES; var ++)

#define POLICY_T(sched, thread) \
  ({                            \
    kassert((thread)->policy < NUM_POLICIES); \
    (&(sched)->policies[(thread)->policy]);   \
  })
#define POLICY_FUNC(policy, func) ({ kassert((policy) < NUM_POLICIES); (policy_impl[(policy)]->func); })
#define POLICY_DATA(sched, policy) ({ kassert((policy) < NUM_POLICIES); ((sched)->policies[(policy)].data); })
#define SCHEDULER(cpu_id) \
  ({                      \
    sched_assert((cpu_id) < _num_schedulers); \
    (_schedulers[cpu_id]);               \
  })

#define SCHED_DISPATCH(sched, policy, func, args...) \
    ((policy < NUM_POLICIES && POLICY_FUNC(policy, func)) ? POLICY_FUNC(policy, func)(POLICY_DATA(sched, policy), ##args) : -1)

#define IS_BLOCKED(thread) ((thread)->status == THREAD_BLOCKED || (thread)->status == THREAD_SLEEPING)

void thread_switch(thread_t *thread);

static const char *sched_reason_str[] = {
  [SCHED_BLOCKED] = "SCHED_BLOCKED",
  [SCHED_PREEMPTED] = "SCHED_PREEMPTED",
  [SCHED_SLEEPING] = "SCHED_SLEEPING",
  [SCHED_TERMINATED] = "SCHED_TERMINATED",
  [SCHED_UPDATED] = "SCHED_UPDATED",
  [SCHED_YIELDED] = "SCHED_YIELDED",
};

static inline thread_status_t get_thread_status(sched_cause_t reason) {
  switch (reason) {
    case SCHED_BLOCKED: return THREAD_BLOCKED;
    case SCHED_PREEMPTED: return THREAD_READY;
    case SCHED_SLEEPING: return THREAD_SLEEPING;
    case SCHED_TERMINATED: return THREAD_TERMINATED;
    case SCHED_UPDATED: return THREAD_READY;
    case SCHED_YIELDED: return THREAD_READY;
    default: unreachable;
  }
}

static inline void register_policy(uint8_t policy, sched_policy_impl_t *impl) {
  kassert(policy < NUM_POLICIES);
  policy_impl[policy] = impl;

  sched_t *sched = PERCPU_SCHED;
  sched->policies[policy].data = impl->init(sched);
  spin_init(&sched->policies[policy].lock);
}

// ----------------------------------------------------------
// These functions dont need to hold locks

static inline void sched_add_ready_thread(sched_t *sched, thread_t *thread) {
  int result = SCHED_DISPATCH(sched, thread->policy, add_thread, thread);
  sched_assert(result == 0);
  sched->ready_count++;
}

void sched_remove_ready_thread(sched_t *sched, thread_t *thread) {
  int result = SCHED_DISPATCH(sched, thread->policy, remove_thread, thread);
  sched_assert(result == 0);
  sched_assert(sched->ready_count > 0);
  sched->ready_count--;
}

void sched_add_blocked_thread(sched_t *sched, thread_t *thread) {
  if (!(thread->flags & F_THREAD_OWN_BLOCKQ)) {
    LIST_ADD(&sched->blocked, thread, list);
  }
  sched->blocked_count++;
}

void sched_remove_blocked_thread(sched_t *sched, thread_t *thread) {
  if (!(thread->flags & F_THREAD_OWN_BLOCKQ)) {
    LIST_REMOVE(&sched->blocked, thread, list);
  }
  kassert(sched->blocked_count > 0);
  sched->blocked_count--;
}

void sched_update_thread_time_end(sched_t *sched, thread_t *thread) {
  // called just after a thread finished a time slice
  sched_assert(thread->stats != NULL);
  sched_stats_t *stats = thread->stats;
  stats->last_active = clock_now();
  SCHED_DISPATCH(sched, thread->policy, on_thread_timeslice_end, thread);
}

void sched_update_thread_time_start(sched_t *sched, thread_t *thread) {
  // called just before a thread resumes a time slice
  sched_assert(thread->stats != NULL);
  sched_stats_t *stats = thread->stats;
  stats->last_scheduled = clock_now();
  stats->sched_count++;
  SCHED_DISPATCH(sched, thread->policy, on_thread_timeslice_start, thread);
}

void sched_update_thread_stats(sched_t *sched, thread_t *thread, sched_cause_t reason) {
  // called before the scheduler switched from `thread` to the next one
  // this is called after `sched_update_thread_time_end`
  sched_assert(thread->stats != NULL);

  sched_stats_t *stats = thread->stats;
  clock_t thread_time = stats->last_active - stats->last_scheduled;
  stats->total_time += thread_time;

  switch (reason) {
    case SCHED_PREEMPTED: stats->preempt_count++; break;
    case SCHED_SLEEPING: stats->sleep_count++; break;
    case SCHED_YIELDED: stats->yield_count++; break;
    default: break;
  }

  SCHED_DISPATCH(sched, thread->policy, on_update_thread_stats, thread, reason);
}

double sched_compute_thread_cpu_affinity_score(sched_t *sched, thread_t *thread) {
  if (POLICY_FUNC(thread->policy, compute_thread_cpu_affinity_score) != NULL) {
    return POLICY_FUNC(thread->policy, compute_thread_cpu_affinity_score)(thread);
  }

  double load_scale = 0.2f;
  if (thread->cpu_id == sched->cpu_id && thread->stats->sched_count > 0) {
    // prefer to schedule onto the same scheduler as before
    load_scale = 0.1f;
  }

  double sched_load = ((sched->ready_count * 0.8) + (sched->blocked_count * 0.2));
  return 1 / (load_scale * sched_load);
}

thread_t *sched_get_next_thread(sched_t *sched) {
  if (sched->ready_count == 0) {
    return sched->idle;
  }

  thread_t *thread = NULL;
  foreach_policy(policy) {
    thread = POLICY_FUNC(policy, get_next_thread)(POLICY_DATA(sched, policy));
    if (thread != NULL) {
      break;
    }
  }

  sched->ready_count--;

  sched_assert(thread != NULL);
  sched_assert(thread->status == THREAD_READY);
  return thread;
}

sched_t *sched_find_cpu_for_thread(thread_t *thread) {
#ifdef SCHED_UNIPROC
  return PERCPU_SCHED;
#endif

  if (thread->affinity >= 0) {
    return SCHEDULER(thread->affinity);
  } else if (thread->stats->sched_count >= SCHED_COUNT_CACHE_AFFINITY_THRES) {
    return SCHEDULER(thread->cpu_id);
  }

  sched_t *best_sched = _schedulers[0];
  foreach_sched(sched) {
    if (sched->total_count <= best_sched->total_count) {
      best_sched = sched;
    }
  }

  // double best_score = sched_compute_thread_cpu_affinity_score(best_sched, thread);
  // foreach_sched(sched) {
  //   if (sched == best_sched) {
  //     continue;
  //   }
  //
  //   double score = sched_compute_thread_cpu_affinity_score(sched, thread);
  //   if (score > best_score) {
  //     best_sched = sched;
  //     best_score = score;
  //   }
  // }

  return best_sched;
}

bool sched_should_preempt(sched_t *sched, thread_t *thread) {
  // determines whether the active thread running on `sched` should be preempted by `thread`
  thread_t *active = sched->active;
  thread_assert(thread, thread != active);
  if (active->preempt_count > 0) {
    return false;
  }

  bool preempt = false;
  if (thread->policy < active->policy) {
    preempt = true;
  } else if (thread->policy == active->policy) {
    if (POLICY_FUNC(thread->policy, should_thread_preempt_same_policy)) {
      preempt = POLICY_FUNC(thread->policy, should_thread_preempt_same_policy)(active, thread);
    } else {
      preempt = thread->priority > active->priority;
    }
  }

  return preempt;
}

// ----------------------------------------------------------
// Locks must be used

void sched_migrate_thread(sched_t *old_sched, sched_t *new_sched, thread_t *thread) {
  sched_assert(old_sched != new_sched);
  sched_assert(thread->cpu_id == old_sched->cpu_id);
  sched_assert(thread->status == THREAD_READY);

  DPRINTF("[CPU#%d] sched: migrating thread %d.%d [%s] from CPU#%d to CPU#%d\n",
          PERCPU_ID, thread->process->pid, thread->tid, thread->name, old_sched->cpu_id, new_sched->cpu_id);
  // DPRINTF("sched: migrating thread %d.%d from CPU#%d to CPU#%d\n",
  //         thread->process->pid, thread->tid, old_sched->cpu_id, new_sched->cpu_id);

  LOCK_THREAD(thread);
  LOCK_SCHED(old_sched);
  LOCK_POLICY(old_sched, thread);
  thread->cpu_id = new_sched->cpu_id;
  SCHED_DISPATCH(old_sched, thread->policy, on_thread_migrate_cpu, thread, new_sched->cpu_id);
  UNLOCK_POLICY(old_sched, thread);
  UNLOCK_SCHED(old_sched);

  LOCK_SCHED(new_sched);
  sched_add_ready_thread(new_sched, thread);
  new_sched->total_count++;
  UNLOCK_SCHED(new_sched);

  UNLOCK_THREAD(thread);
}

//

noreturn void *sched_idle_thread(void *arg) {
  sched_t *sched = PERCPU_SCHED;

  uint64_t retries = 0;
  clock_t expire = clock_future_time(MS_TO_NS(1000));
  while (true) {
    clock_t now = clock_now();
    if (now >= expire) {
      if (retries > UINT64_MAX) {
        panic("stuck in idle");
        unreachable;
      }

      alarm_reschedule();
      expire = now + MS_TO_NS(1000);
      retries++;
    }

    LOCK_SCHED(sched);
    if (sched->ready_count > 0) {
      retries = 0;
      DPRINTF("sched: exiting idle [CPU#%d]\n", PERCPU_ID);

      UNLOCK_SCHED(sched);
      sched_yield();
      expire = now + MS_TO_NS(1000);
    } else {
      UNLOCK_SCHED(sched);
    }

    cpu_pause();
    cpu_pause();
    cpu_pause();
  }
  unreachable;
}

//

noreturn void sched_init(process_t *root) {
  static bool done = false;
  if (!PERCPU_IS_BSP) {
    // AP processors need to wait for BSP to finish
    kprintf("sched: CPU#%d waiting\n", PERCPU_ID);
    while (!done) cpu_pause();
    root = process_get(0);
    kassert(root != NULL);
  }

  id_t tid = atomic_fetch_add(&root->num_threads, 1);
  thread_t *idle = thread_alloc(tid, sched_idle_thread, NULL, false);
  idle->process = root;
  idle->priority = 255;
  idle->policy = 255;
  idle->name = kasprintf("idle.%d", PERCPU_ID);
  LOCK(root);
  LIST_ADD(&root->threads, idle, group);
  UNLOCK(root);

  sched_t *sched = kmalloc(sizeof(sched_t));
  sched->cpu_id = PERCPU_ID;
  spin_init(&sched->lock);

  sched->ready_count = 0;
  sched->blocked_count = 0;
  sched->total_count = PERCPU_IS_BSP ? 1 : 0;
  sched->idle_time = 0;

  sched->active = PERCPU_IS_BSP ? root->main : idle;
  sched->idle = idle;

  LIST_INIT(&sched->blocked);

  foreach_policy(policy) {
    sched->policies[policy].data = NULL;
    spin_init(&sched->policies[policy].lock);
  }

  PERCPU_SET_SCHED(sched);
  _schedulers[sched->cpu_id] = sched;
  atomic_fetch_add(&_num_schedulers, 1);

  // register policies
  register_policy(POLICY_SYSTEM, &sched_policy_fprr);
  register_policy(POLICY_DRIVER, &sched_policy_fprr);

  init_oneshot_timer();
  timer_enable(TIMER_ONE_SHOT);
  if (PERCPU_IS_BSP) {
    thread_t *thread = root->main;
    SCHED_DISPATCH(sched, thread->policy, policy_init_thread, thread);
    sched->total_count++;
    sched_add_ready_thread(sched, thread);
    done = true;
  }

  kprintf("sched: CPU#%d initialized\n", PERCPU_ID);

  if (PERCPU_IS_BSP) {
    // wait until all schedulers are initialized
    while (_num_schedulers != system_num_cpus) {
      cpu_pause();
    }
  }

  sched_reschedule(SCHED_UPDATED);
  unreachable;
}

int sched_add(thread_t *thread) {
  sched_assert(thread->stats != NULL);
  sched_assert(thread->policy < NUM_POLICIES);
  sched_t *sched = sched_find_cpu_for_thread(thread);

  kprintf("[CPU#%d] sched: adding thread %d.%d [%s] to CPU#%d\n",
          PERCPU_ID, thread->process->pid, thread->tid, thread->name, sched->cpu_id);

  uint64_t flags;
  temp_irq_save(flags);
  LOCK_SCHED(sched);
  LOCK_THREAD(thread);
  LOCK_POLICY(sched, thread);

  thread->cpu_id = sched->cpu_id;
  thread->status = THREAD_READY;
  SCHED_DISPATCH(sched, thread->policy, policy_init_thread, thread);
  sched->total_count++;
  sched_add_ready_thread(sched, thread);

  UNLOCK_POLICY(sched, thread);
  UNLOCK_THREAD(thread);
  UNLOCK_SCHED(sched);
  temp_irq_restore(flags);

  if (sched_should_preempt(sched, thread)) {
    if (thread->cpu_id == PERCPU_ID) {
      // reschedule current cpu
      return sched_reschedule(SCHED_PREEMPTED);
    }

    DPRINTF("[CPU#%d] sched: sending ipi to CPU#%d\n", PERCPU_ID, thread->cpu_id);
    return ipi_deliver_cpu_id(IPI_SCHEDULE, thread->cpu_id, SCHED_PREEMPTED);
  }
  return 0;
}

int sched_terminate(thread_t *thread) {
  sched_assert(thread->status != THREAD_TERMINATED);
  sched_t *sched = SCHEDULER(thread->cpu_id);

  if (thread->status == THREAD_RUNNING) {
    if (thread->cpu_id == PERCPU_ID) {
      // reschedule current cpu and let the scheduler clean up on next pass
      return sched_reschedule(SCHED_TERMINATED);
    }
    return ipi_deliver_cpu_id(IPI_SCHEDULE, thread->cpu_id, SCHED_TERMINATED);
  }

  uint64_t flags;
  temp_irq_save(flags);
  LOCK_SCHED(sched);
  LOCK_THREAD(thread);
  LOCK_POLICY(sched, thread);

  if (thread->status == THREAD_READY) {
    sched_remove_ready_thread(sched, thread);
  } else if (thread->status == THREAD_BLOCKED) {
    sched_remove_blocked_thread(sched, thread);
  } else if (thread->status == THREAD_SLEEPING) {
    // TODO: support canceling a timer on another cpu
    sched_assert(thread->cpu_id == PERCPU_ID);
    timer_delete_alarm(thread->alarm_id);
    sched_remove_blocked_thread(sched, thread);
  } else {
    panic("sched_terminate: not implemented");
  }

  thread->status = THREAD_TERMINATED;
  sched->total_count--;
  SCHED_DISPATCH(sched, thread->policy, policy_deinit_thread, thread);

  UNLOCK_POLICY(sched, thread);
  UNLOCK_THREAD(thread);
  UNLOCK_SCHED(sched);
  temp_irq_restore(flags);
  return 0;
}

int sched_block(thread_t *thread) {
  sched_t *sched = SCHEDULER(thread->cpu_id);
  sched_assert(!IS_BLOCKED(thread));

  DPRINTF("[CPU#%d] sched: blocking thread %d.%d [%s] on CPU#%d\n",
          PERCPU_ID, thread->process->pid, thread->tid, thread->name, sched->cpu_id);
  if (thread->status == THREAD_RUNNING) {
    if (thread->cpu_id == PERCPU_ID) {
      // reschedule current cpu
      return sched_reschedule(SCHED_BLOCKED);
    }

    DPRINTF("[CPU#%d] sched: sending ipi to CPU#%d\n", PERCPU_ID, thread->cpu_id);
    return ipi_deliver_cpu_id(IPI_SCHEDULE, thread->cpu_id, SCHED_BLOCKED);
  }

  uint64_t flags;
  temp_irq_save(flags);
  LOCK_SCHED(sched);
  LOCK_THREAD(thread);
  LOCK_POLICY(sched, thread);

  sched_assert(thread->status == THREAD_READY);
  sched_remove_ready_thread(sched, thread);
  thread->status = THREAD_BLOCKED;
  sched_add_blocked_thread(sched, thread);

  UNLOCK_POLICY(sched, thread);
  UNLOCK_THREAD(thread);
  UNLOCK_SCHED(sched);
  temp_irq_restore(flags);
  return 0;
}

int sched_unblock(thread_t *thread) {
  sched_t *sched = SCHEDULER(thread->cpu_id);
  if (!IS_BLOCKED(thread)) {
    panic("thread %d.%d not blocked [%s]", thread->tid, thread->process->pid, thread->name);
  }

  DPRINTF("[CPU#%d] sched: unblocking thread %d.%d [%s] on CPU#%d\n",
          PERCPU_ID, thread->process->pid, thread->tid, thread->name, sched->cpu_id);

  uint64_t flags;
  temp_irq_save(flags);
  LOCK_SCHED(sched);
  LOCK_THREAD(thread);
  LOCK_POLICY(sched, thread);

  sched_remove_blocked_thread(sched, thread);
  thread->status = THREAD_READY;
  sched_add_ready_thread(sched, thread);

  UNLOCK_POLICY(sched, thread);
  UNLOCK_THREAD(thread);
  UNLOCK_SCHED(sched);
  temp_irq_restore(flags);

  if (sched_should_preempt(sched, thread)) {
    if (thread->cpu_id == PERCPU_ID) {
      // reschedule current cpu
      return sched_reschedule(SCHED_PREEMPTED);
    }

    DPRINTF("[CPU#%d] sched: sending ipi to CPU#%d\n", PERCPU_ID, thread->cpu_id);
    return ipi_deliver_cpu_id(IPI_SCHEDULE, thread->cpu_id, SCHED_PREEMPTED);
  }
  return 0;
}

int sched_wakeup(thread_t *thread) {
  sched_t *sched = SCHEDULER(thread->cpu_id);
  sched_assert(thread->status == THREAD_SLEEPING);

  DPRINTF("[CPU#%d] sched: waking up thread %d.%d [%s] on CPU#%d\n",
          PERCPU_ID, thread->process->pid, thread->tid, thread->name, thread->cpu_id);

  uint64_t flags;
  temp_irq_save(flags);
  LOCK_SCHED(sched);
  LOCK_THREAD(thread);
  LOCK_POLICY(sched, thread);

  sched_remove_blocked_thread(sched, thread);
  thread->status = THREAD_READY;
  sched_add_ready_thread(sched, thread);

  UNLOCK_POLICY(sched, thread);
  UNLOCK_THREAD(thread);
  UNLOCK_SCHED(sched);
  temp_irq_restore(flags);

  if (sched_should_preempt(sched, thread)) {
    if (thread->cpu_id == PERCPU_ID) {
      // reschedule current cpu
      return sched_reschedule(SCHED_PREEMPTED);
    }
    // rescheduler another cpu

    DPRINTF("[CPU#%d] sched: sending ipi to CPU#%d\n", PERCPU_ID, thread->cpu_id);
    return ipi_deliver_cpu_id(IPI_SCHEDULE, thread->cpu_id, SCHED_PREEMPTED);
  }
  return 0;
}

int sched_setsched(sched_opts_t opts) {
  thread_t *thread = PERCPU_THREAD;
  sched_t *sched = SCHEDULER(thread->cpu_id);

  uint64_t flags;
  temp_irq_save(flags);
  LOCK_SCHED(sched);
  LOCK_THREAD(thread);
  LOCK_POLICY(sched, thread);

  if (opts.policy != thread->policy) {
    SCHED_DISPATCH(sched, thread->policy, policy_deinit_thread, thread);
    thread->policy = opts.policy;
    SCHED_DISPATCH(sched, thread->policy, policy_init_thread, thread);
  }
  if (opts.priority != thread->priority) {
    thread->priority = opts.priority;
  }
  if (opts.affinity != thread->affinity) {
    DPRINTF("[CPU#%d] sched: changing affinity of thread %d.%d [%s] to %d\n",
            PERCPU_ID, thread->process->pid, thread->tid, thread->name, opts.affinity);
    kassert(opts.affinity < system_num_cpus);
    thread->affinity = opts.affinity;
    if (opts.affinity >= 0 && opts.affinity != PERCPU_ID) {
      UNLOCK_POLICY(sched, thread);
      UNLOCK_THREAD(thread);
      UNLOCK_SCHED(sched);
      temp_irq_restore(flags);
      return sched_reschedule(SCHED_UPDATED);
    }
  }

  UNLOCK_POLICY(sched, thread);
  UNLOCK_THREAD(thread);
  UNLOCK_SCHED(sched);
  temp_irq_restore(flags);
  return 0;
}

int sched_sleep(uint64_t ns) {
  thread_t *thread = PERCPU_THREAD;
  sched_assert(thread->status == THREAD_RUNNING);

  DPRINTF("[CPU#%d] sched: sleeping thread %d.%d [%s]\n",
          PERCPU_ID, getpid(), gettid(), thread->name, thread->cpu_id);

  clock_t now = clock_now();
  thread->alarm_id = timer_create_alarm(now + ns, (void *) sched_wakeup, thread);
  if (thread->alarm_id < 0) {
    panic("failed to create alarm\n");
  }
  return sched_reschedule(SCHED_SLEEPING);
}

int sched_yield() {
  thread_t *thread = PERCPU_THREAD;
  sched_assert(thread->status == THREAD_RUNNING);

  DPRINTF("[CPU#%d] sched: yielding thread %d.%d [%s]\n",
          PERCPU_ID, getpid(), gettid(), thread->name, thread->cpu_id);

  return sched_reschedule(SCHED_YIELDED);
}

//

int sched_reschedule(sched_cause_t reason) {
  DPRINTF("[CPU#%d] sched: rescheduling [%s]\n", PERCPU_ID, sched_reason_str[reason]);

  uint64_t flags;
  temp_irq_save(flags);

  sched_assert(reason <= SCHED_YIELDED);
  sched_t *sched = PERCPU_SCHED;
  thread_t *curr = PERCPU_THREAD;
  if (curr == NULL) {
    // first time skip unlocks
    LOCK_SCHED(sched);
    goto next_thread_first;
  }

  // --------------

  sched_assert(curr->cpu_id == sched->cpu_id);
  if (reason == SCHED_PREEMPTED && curr->preempt_count > 0) {
    curr->preempt_count--;
    goto end;
  } else if (reason == SCHED_PREEMPTED && sched->ready_count == 0) {
    goto end;
  }

  LOCK_THREAD(curr);
  sched_update_thread_time_end(sched, curr);
  curr->status = get_thread_status(reason);
  sched_update_thread_stats(sched, curr, reason);

  if (reason == SCHED_UPDATED) {
    sched_assert(curr != sched->idle);
    if (curr->affinity >= 0 && curr->affinity != PERCPU_ID) {
      sched_assert(curr->affinity < _num_schedulers);
      sched_t *new_sched = SCHEDULER(curr->affinity);
      sched_migrate_thread(sched, new_sched, curr);
      LOCK_SCHED(sched);
      goto next_thread;
    }
  }

  LOCK_SCHED(sched);
  if (curr != sched->idle)
    LOCK_POLICY(sched, curr);

  if (curr == sched->idle) {
    curr->status = THREAD_READY;
    sched->idle_time += curr->stats->last_active - curr->stats->last_scheduled;
  } else if (IS_BLOCKED(curr)) {
    sched_add_blocked_thread(sched, curr);
  } else if (curr->status == THREAD_TERMINATED) {
    sched->total_count--;
    SCHED_DISPATCH(sched, curr->policy, policy_deinit_thread, curr);
  } else {
    sched_assert(curr->status == THREAD_READY);
    sched_add_ready_thread(sched, curr);
  }

  if (curr != sched->idle)
    UNLOCK_POLICY(sched, curr);

LABEL(next_thread);
  UNLOCK_THREAD(curr);

LABEL(next_thread_first);
  thread_t *next = sched_get_next_thread(sched);

  LOCK_THREAD(next);
  if (next != sched->idle)
    LOCK_POLICY(sched, next);

  next->status = THREAD_RUNNING;
  sched->active = next;
  sched_update_thread_time_start(sched, next);

  if (next != sched->idle)
    UNLOCK_POLICY(sched, next);
  UNLOCK_THREAD(next);
  UNLOCK_SCHED(sched);

  if (next != curr) {
    if (curr != NULL) {
      DPRINTF("[CPU#%d] sched: switching from thread %d.%d [%s] to %d.%d [%s]\n",
              PERCPU_ID,
              curr->process->pid, curr->tid, curr->name,
              next->process->pid, next->tid, next->name);
    }

    temp_irq_restore(flags);
    thread_switch(next);
    DPRINTF("[CPU#%d] sched: now in thread %d.%d [%s]\n",
            PERCPU_ID, PERCPU_THREAD->process->pid, PERCPU_THREAD->tid, PERCPU_THREAD->name);
    return 0;
  }

LABEL(end);
  temp_irq_restore(flags);
  DPRINTF("[CPU#%d] sched: continuing in thread %d.%d [%s]\n",
          PERCPU_ID, PERCPU_THREAD->process->pid, PERCPU_THREAD->tid, PERCPU_THREAD->name);
  return 0;
}
