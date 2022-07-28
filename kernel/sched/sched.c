//
// Created by Aaron Gill-Braun on 2022-07-24.
//

#include <sched/sched.h>
#include <sched/fprr.h>

#include <cpu/cpu.h>

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

#define sched_assert(expr) kassert(expr)
// #define sched_assert(expr)

#define foreach_sched(var) \
  sched_t * var = NULL; int i = 0; \
  for (i = 0, var = _schedulers[i]; i < _num_schedulers; i++, var = _schedulers[i])

#define foreach_policy(var) \
  uint16_t var = 0; \
  for (var = 0; var < NUM_POLICIES; var ++)

#define POLICY_FUNC(policy, func) (policy_impl[(policy)]->func)
#define POLICY_DATA(sched, policy) ((sched)->policies[(policy)])
#define SCHEDULER(cpu_id) \
  ({                      \
    sched_assert((cpu_id) < _num_schedulers); \
    (_schedulers[cpu_id]);               \
  })

#define SCHED_DISPATCH(sched, policy, func, args...) \
    ((policy < NUM_POLICIES && POLICY_FUNC(policy, func)) ? POLICY_FUNC(policy, func)(POLICY_DATA(sched, policy), ##args) : -1)

#define IS_BLOCKED(thread) ((thread)->status == THREAD_BLOCKED || (thread)->status == THREAD_SLEEPING)

noreturn void thread_switch(thread_t *thread);

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

static inline void sched_increment_count(size_t *count) {
  sched_assert(count != NULL);
  atomic_fetch_add(count, 1);
}

static inline void sched_decrement_count(size_t *count) {
  sched_assert(count != NULL);
  sched_assert(*count > 0);
  atomic_fetch_sub(count, 1);
}

static inline void register_policy(uint8_t policy, sched_policy_impl_t *impl) {
  kassert(policy < NUM_POLICIES);
  policy_impl[policy] = impl;

  sched_t *sched = PERCPU_SCHED;
  sched->policies[policy] = impl->init(sched);
}

//

void sched_add_ready_thread(sched_t *sched, thread_t *thread) {
  spin_lock(&sched->lock);
  int result = SCHED_DISPATCH(sched, thread->policy, add_thread, thread);
  sched_assert(result >= 0);

  sched_increment_count(&sched->ready_count);
  spin_unlock(&sched->lock);
}

void sched_remove_ready_thread(sched_t *sched, thread_t *thread) {
  spin_lock(&sched->lock);
  int result = SCHED_DISPATCH(sched, thread->policy, remove_thread, thread);
  sched_assert(result >= 0);

  sched_decrement_count(&sched->ready_count);
  spin_unlock(&sched->lock);
}

void sched_add_blocked_thread(sched_t *sched, thread_t *thread) {
  spin_lock(&sched->lock);
  if (!(thread->flags & F_THREAD_OWN_BLOCKQ)) {
    LIST_ADD(&sched->blocked, thread, list);
  }

  sched_increment_count(&sched->blocked_count);
  spin_unlock(&sched->lock);
}

void sched_remove_blocked_thread(sched_t *sched, thread_t *thread) {
  spin_lock(&sched->lock);
  if (!(thread->flags & F_THREAD_OWN_BLOCKQ)) {
    LIST_REMOVE(&sched->blocked, thread, list);
  }

  sched_decrement_count(&sched->blocked_count);
  spin_unlock(&sched->lock);
}

void sched_migrate_thread(sched_t *old_sched, sched_t *new_sched, thread_t *thread) {
  sched_assert(old_sched != new_sched);
  sched_assert(thread->cpu_id == old_sched->cpu_id);
  sched_assert(thread->status == THREAD_READY);

  SCHED_DISPATCH(old_sched, thread->policy, on_thread_migrate_cpu, thread, new_sched->cpu_id);

  kprintf("sched: migrating thread %d.%d from CPU#%d to CPU#%d\n",
          thread->process->pid, thread->tid, old_sched->cpu_id, new_sched->cpu_id);

  thread->cpu_id = new_sched->cpu_id;
  sched_decrement_count(&old_sched->total_count);
  sched_add_ready_thread(new_sched, thread);
  sched_increment_count(&new_sched->total_count);
}

//

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

//

double sched_compute_thread_cpu_affinity_score(sched_t *sched, thread_t *thread) {
  if (POLICY_FUNC(thread->policy, compute_thread_cpu_affinity_score) != NULL) {
    return POLICY_FUNC(thread->policy, compute_thread_cpu_affinity_score)(thread);
  }

  double load_scale = 0.2f;
  if (thread->cpu_id == sched->cpu_id && thread->stats->sched_count > 0) {
    // slightly prefer to schedule onto the same scheduler as before
    load_scale = 0.15f;
  }

  double sched_load = ((sched->ready_count * 0.8) + (sched->blocked_count * 0.2));
  return 1 / ((load_scale * sched_load) + 1);
}

thread_t *sched_get_next_thread(sched_t *sched) {
  spin_lock(&sched->lock);
  if (sched->ready_count == 0) {
    spin_unlock(&sched->lock);
    return sched->idle;
  }

  thread_t *thread = NULL;
  foreach_policy(policy) {
    thread = POLICY_FUNC(policy, get_next_thread)(POLICY_DATA(sched, policy));
    if (thread != NULL) {
      break;
    }
  }

  sched_decrement_count(&sched->ready_count);
  spin_unlock(&sched->lock);

  sched_assert(thread != NULL);
  sched_assert(thread->status == THREAD_READY);
  return thread;
}

sched_t *sched_find_cpu_for_thread(thread_t *thread) {
  if (thread->affinity >= 0) {
    return SCHEDULER(thread->affinity);
  } else if (thread->stats->sched_count >= SCHED_COUNT_CACHE_AFFINITY_THRES) {
    return SCHEDULER(thread->cpu_id);
  }

  sched_t *best_sched = SCHEDULER(thread->cpu_id);
  double best_score = sched_compute_thread_cpu_affinity_score(best_sched, thread);
  foreach_sched(sched) {
    if (sched == best_sched) {
      continue;
    }

    double score = sched_compute_thread_cpu_affinity_score(sched, thread);
    if (score > best_score) {
      best_sched = sched;
      best_score = score;
    }
  }

  return best_sched;
}

bool sched_should_preempt(sched_t *sched, thread_t *thread) {
  // determines whether the active thread running on `sched` should be preempted by `thread`
  spin_lock(&sched->lock);
  thread_t *active = sched->active;
  sched_assert(thread != active);

  if (active->preempt_count > 0) {
    spin_unlock(&sched->lock);
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

  spin_unlock(&sched->lock);
  return preempt;
}

//

noreturn void *sched_idle_thread(void *arg) {
  sched_t *sched = PERCPU_SCHED;
  while (true) {
    if (sched->ready_count > 0) {
      sched_yield();
    }
    cpu_pause();
  }
  unreachable;
}

//

noreturn void sched_init(process_t *root) {
  while (root == NULL) {
    // AP processors need to wait for BSP to finish
    sched_assert(!PERCPU_IS_BSP);
    timer_udelay(10);
    root = process_get(0);
  }

  thread_t *idle = thread_alloc(LIST_LAST(&root->threads)->tid + 1, sched_idle_thread, NULL, false);
  idle->process = root;
  idle->priority = 255;
  idle->policy = 255;
  LIST_ADD(&root->threads, idle, group);

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
    sched->policies[policy] = NULL;
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
    sched_add_ready_thread(sched, thread);
    sched_increment_count(&sched->total_count);
  }

  sched_reschedule(SCHED_UPDATED);
  unreachable;
}

int sched_add(thread_t *thread) {
  sched_assert(thread->stats != NULL);
  sched_assert(thread->policy < NUM_POLICIES);
  sched_t *sched = sched_find_cpu_for_thread(thread);

  kprintf("sched: scheduling thread %d.%d onto CPU#%d\n",
          thread->process->pid, thread->tid, sched->cpu_id);

  thread->cpu_id = sched->cpu_id;
  thread->status = THREAD_READY;
  SCHED_DISPATCH(sched, thread->policy, policy_init_thread, thread);
  sched_add_ready_thread(sched, thread);
  sched_increment_count(&sched->total_count);

  if (sched_should_preempt(sched, thread)) {
    if (thread->cpu_id == PERCPU_ID) {
      // reschedule current cpu
      return sched_reschedule(SCHED_PREEMPTED);
    }
    return ipi_deliver_cpu_id(IPI_SCHEDULE, thread->cpu_id, SCHED_PREEMPTED);
  }
  return 0;
}

int sched_terminate(thread_t *thread) {
  sched_assert(thread->status != THREAD_TERMINATED);
  sched_t *sched = SCHEDULER(thread->cpu_id);

  if (thread->status == THREAD_RUNNING) {
    if (thread->cpu_id == PERCPU_ID) {
      // reschedule current cpu
      return sched_reschedule(SCHED_TERMINATED);
    }
    return ipi_deliver_cpu_id(IPI_SCHEDULE, thread->cpu_id, SCHED_TERMINATED);
  }

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
  sched_decrement_count(&sched->total_count);
  SCHED_DISPATCH(sched, thread->policy, policy_deinit_thread, thread);
  return 0;
}

int sched_block(thread_t *thread) {
  sched_t *sched = SCHEDULER(thread->cpu_id);
  sched_assert(!IS_BLOCKED(thread));

  if (thread->status == THREAD_RUNNING) {
    if (thread->cpu_id == PERCPU_ID) {
      // reschedule current cpu
      return sched_reschedule(SCHED_BLOCKED);
    }
    return ipi_deliver_cpu_id(IPI_SCHEDULE, thread->cpu_id, SCHED_BLOCKED);
  }

  sched_assert(thread->status == THREAD_READY);
  sched_remove_ready_thread(sched, thread);
  thread->status = THREAD_BLOCKED;
  sched_add_blocked_thread(sched, thread);
  return 0;
}

int sched_unblock(thread_t *thread) {
  sched_t *sched = SCHEDULER(thread->cpu_id);
  sched_assert(thread->status == THREAD_BLOCKED);

  sched_remove_blocked_thread(sched, thread);
  thread->status = THREAD_READY;
  sched_add_ready_thread(sched, thread);

  if (sched_should_preempt(sched, thread)) {
    if (thread->cpu_id == PERCPU_ID) {
      // reschedule current cpu
      return sched_reschedule(SCHED_PREEMPTED);
    }
    return ipi_deliver_cpu_id(IPI_SCHEDULE, thread->cpu_id, SCHED_PREEMPTED);
  }
  return 0;
}

int sched_wakeup(thread_t *thread) {
  sched_t *sched = SCHEDULER(thread->cpu_id);
  sched_assert(thread->status == THREAD_SLEEPING);

  sched_remove_blocked_thread(sched, thread);
  thread->status = THREAD_READY;
  sched_add_ready_thread(sched, thread);

  if (sched_should_preempt(sched, thread)) {
    if (thread->cpu_id == PERCPU_ID) {
      // reschedule current cpu
      return sched_reschedule(SCHED_PREEMPTED);
    }
    return ipi_deliver_cpu_id(IPI_SCHEDULE, thread->cpu_id, SCHED_PREEMPTED);
  }
  return 0;
}

int sched_setsched(sched_opts_t opts) {
  thread_t *thread = PERCPU_THREAD;
  sched_t *sched = SCHEDULER(thread->cpu_id);

  if (opts.policy != thread->policy) {
    SCHED_DISPATCH(sched, thread->policy, policy_deinit_thread, thread);
    thread->policy = opts.policy;
    SCHED_DISPATCH(sched, thread->policy, policy_init_thread, thread);
  }
  if (opts.priority != thread->priority) {
    thread->priority = opts.priority;
  }
  if (opts.affinity != thread->affinity) {
    thread->affinity = opts.affinity;
    if (opts.affinity >= 0 && opts.affinity != PERCPU_ID) {
      return sched_reschedule(SCHED_UPDATED);
    }
  }
  return 0;
}

int sched_sleep(uint64_t ns) {
  thread_t *thread = PERCPU_THREAD;
  sched_assert(thread->status == THREAD_RUNNING);
  thread->alarm_id = timer_create_alarm(timer_now() + ns, (void *) sched_wakeup, thread);
  return sched_reschedule(SCHED_SLEEPING);
}

int sched_yield() {
  thread_t *thread = PERCPU_THREAD;
  sched_assert(thread->status == THREAD_RUNNING);
  return sched_reschedule(SCHED_YIELDED);
}

//

int sched_reschedule(sched_cause_t reason) {
  sched_assert(reason <= SCHED_YIELDED);
  sched_t *sched = PERCPU_SCHED;
  thread_t *curr = PERCPU_THREAD;
  if (curr == NULL) {
    goto next_thread;
  }

  sched_assert(curr->cpu_id == sched->cpu_id);
  if (reason == SCHED_PREEMPTED && curr->preempt_count > 0) {
    curr->preempt_count--;
    return 0;
  } else if (reason == SCHED_PREEMPTED && sched->ready_count == 0) {
    return 0;
  }

  sched_update_thread_time_end(sched, curr);
  curr->status = get_thread_status(reason);
  sched_update_thread_stats(sched, curr, reason);

  if (reason == SCHED_UPDATED) {
    sched_assert(curr != sched->idle);
    if (curr->affinity >= 0 && curr->affinity != PERCPU_ID) {
      sched_assert(curr->affinity < _num_schedulers);
      sched_t *new_sched = SCHEDULER(curr->affinity);
      sched_migrate_thread(sched, new_sched, curr);
      goto next_thread;
    }
  }

  if (curr == sched->idle) {
    curr->status = THREAD_READY;
    sched->idle_time += curr->stats->last_active - curr->stats->last_scheduled;
  } else if (IS_BLOCKED(curr)) {
    sched_add_blocked_thread(sched, curr);
  } else if (curr->status == THREAD_TERMINATED) {
    sched_decrement_count(&sched->total_count);
    SCHED_DISPATCH(sched, curr->policy, policy_deinit_thread, curr);
  } else {
    sched_assert(curr->status == THREAD_READY);
    sched_add_ready_thread(sched, curr);
  }

LABEL(next_thread);
  thread_t *next = sched_get_next_thread(sched);
  next->status = THREAD_RUNNING;
  sched_update_thread_time_start(sched, next);
  sched->active = next;
  if (next != curr) {
    thread_switch(next);
  }
  return 0;
}
