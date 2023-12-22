//
// Created by Aaron Gill-Braun on 2022-07-24.
//

#include <kernel/sched.h>
#include <kernel/process.h>

#include <kernel/cpu/cpu.h>

#include <kernel/clock.h>
#include <kernel/timer.h>
#include <kernel/ipi.h>

#include <kernel/printf.h>
#include <kernel/panic.h>
#include <atomic.h>

// #define ASSERT(x)
#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...)
// #define DPRINTF(x, ...) kprintf("sched: " x, ##__VA_ARGS__)

#define POLICY_FUNC(policy, func) ({ ASSERT((policy) < NUM_POLICIES); (policy_impls[(policy)]->func); })
#define POLICY_DATA(sched, policy) ({ ASSERT((policy) < NUM_POLICIES); ((sched)->policies[(policy)].data); })
#define POLICY_DISPATCH_D(sched, policy, func, noexist, args...) \
  (POLICY_FUNC(policy, func) ? POLICY_FUNC(policy, func)(POLICY_DATA(sched, policy), ##args) : (noexist))
#define POLICY_DISPATCH(sched, policy, func, args...) POLICY_DISPATCH_D(sched, policy, func, -ENOTSUP, ##args)

#define SCHED_POLICY(sched, td) (&(sched)->policies[(td)->policy])
#define LOCK_POLICY(sched, td) (spin_lock(&SCHED_POLICY(sched, td)->lock))
#define UNLOCK_POLICY(sched, td) (spin_unlock(&SCHED_POLICY(sched, td)->lock))

void sched_switch(thread_t *curr, thread_t *next, mutex_t *cur_lock);

sched_policy_impl_t *policy_impls[NUM_POLICIES];
sched_t *schedulers[MAX_CPUS];

static const char *sched_reason_str[] = {
  [SCHR_BLOCKED] = "SCHR_BLOCKED",
  [SCHR_PREEMPTED] = "SCHR_PREEMPTED",
  [SCHR_SLEEPING] = "SCHR_SLEEPING",
  [SCHR_TERMINATED] = "SCHR_TERMINATED",
  [SCHR_UPDATED] = "SCHR_UPDATED",
  [SCHR_YIELDED] = "SCHR_YIELDED",
};

// ----------------------------------------------------------
// sched lock is held while calling these functions

static void sched_add_ready_thread(sched_t *sched, thread_t *td) {
  int result = POLICY_DISPATCH(sched, td->policy, add_thread, td);
  ASSERT(result == 0);
  sched->ready_count++;
}

static void sched_remove_ready_thread(sched_t *sched, thread_t *td) {
  ASSERT(sched->ready_count > 0);
  int result = POLICY_DISPATCH(sched, td->policy, remove_thread, td);
  ASSERT(result == 0);
  sched->ready_count--;
}

static void sched_before_thread_timeslice_start(sched_t *sched, thread_t *td) {
  LOCK_TD_STATS(td);
  struct thread_stats *stats = &td->stats;
  stats->last_scheduled = clock_now();
  stats->switches++;
  POLICY_DISPATCH(sched, td->policy, on_thread_timeslice_start, td);
  UNLOCK_TD_STATS(td);
}

static void sched_after_thread_timeslice_end(sched_t *sched, thread_t *td, sched_reason_t reason) {
  LOCK_TD_STATS(td);
  struct thread_stats *stats = &td->stats;
  struct rusage *usage = &td->usage;
  stats->last_active = clock_now();

  uint64_t inc_runtime = stats->last_active - stats->last_scheduled;
  stats->runtime += inc_runtime;
  stats->last_scheduled = stats->last_active;
  atomic_fetch_add(&td->process->total_runtime, inc_runtime);
  switch (reason) {
    case SCHR_PREEMPTED:
      stats->preempted++;
      break;
    case SCHR_BLOCKED:
      stats->blocks++;
      break;
    case SCHR_SLEEPING:
      stats->sleeps++;
      break;
    case SCHR_YIELDED:
      stats->yields++;
      break;
    case SCHR_TERMINATED:
    case SCHR_UPDATED:
      break;
  }

  usage->ru_utime.tv_sec = (time_t) (stats->runtime / NS_PER_SEC);
  usage->ru_utime.tv_usec = (suseconds_t) ((stats->runtime % NS_PER_SEC) / NS_PER_USEC);
  POLICY_DISPATCH(sched, td->policy, on_thread_timeslice_end, td, reason);
  UNLOCK_TD_STATS(td);
}

static thread_t *sched_get_next_thread(sched_t *sched) __move {
  if (sched->ready_count == 0) {
    return sched->idle_td;
  }

  // check every policy for a thread to run
  thread_t *td = NULL;
  for (uint8_t p = 0; !td && p < NUM_POLICIES; p++) {
    td = POLICY_DISPATCH_D(sched, p, get_next_thread, NULL);
  }
  if (td != NULL)
    return NULL;

  // td->lock is already held
  ASSERT(td->state == TDS_READY);
  td->state = TDS_RUNNING;
  sched->ready_count--;
  return td;
}

static bool sched_should_preempt(sched_t *sched, thread_t *other) {
  thread_t *active = sched->active_td;
  ASSERT(active != other);
  if (active == sched->idle_td) {
    // any thread can preempt idle
    return true;
  }

  if (other->policy < active->policy) {
    return true;
  } else if (other->policy == active->policy) {
    if (POLICY_FUNC(other->policy, should_thread_preempt_same_policy)) {
      return POLICY_FUNC(other->policy, should_thread_preempt_same_policy)(active, other);
    } else {
      return other->priority > active->priority;
    }
  }
  return false;
}

// ----------------------------------------------------------
// locks must be used

static sched_t *select_cpu_sched_for_thread(thread_t *td) {
#ifndef SCHED_UNIPROC
  // TODO: improve this by selecting the cpu with the least number of threads
  int cpu = -1;
  if ((cpu = cpuset_next_set(td->cpuset, cpu)) >= 0) {
    // return the first cpu allowed
    ASSERT(cpu < MAX_CPUS);
    return schedulers[cpu];
  }
#endif
  return PERCPU_SCHED;
}

static void sched_do_reschedule(sched_t *sched, sched_reason_t reason) {
  if (sched == PERCPU_SCHED) {
    reschedule(reason);
  } else {
    ipi_deliver_cpu_id(IPI_SCHEDULE, sched->cpu_id, reason);
  }
}

void sched_migrate_thread(sched_t *old_sched, sched_t *new_sched, thread_t *thread) {
  // sched_assert(old_sched != new_sched);
  // sched_assert(thread->cpu_id == old_sched->cpu_id);
  // sched_assert(thread->status == THREAD_READY);
  //
  // DPRINTF("[CPU#%d] sched: migrating thread %d.%d [%s] from CPU#%d to CPU#%d\n",
  //         PERCPU_ID, thread->process->pid, thread->tid, thread->name, old_sched->cpu_id, new_sched->cpu_id);
  // // DPRINTF("sched: migrating thread %d.%d from CPU#%d to CPU#%d\n",
  // //         thread->process->pid, thread->tid, old_sched->cpu_id, new_sched->cpu_id);
  //
  // LOCK_THREAD(thread);
  // LOCK_SCHED(old_sched);
  // LOCK_POLICY(old_sched, thread);
  // thread->cpu_id = new_sched->cpu_id;
  // SCHED_DISPATCH(old_sched, thread->policy, on_thread_migrate_cpu, thread, new_sched->cpu_id);
  // UNLOCK_POLICY(old_sched, thread);
  // UNLOCK_SCHED(old_sched);
  //
  // LOCK_SCHED(new_sched);
  // sched_add_ready_thread(new_sched, thread);
  // new_sched->total_count++;
  // UNLOCK_SCHED(new_sched);
  //
  // UNLOCK_THREAD(thread);
  todo();
}

//

int sched_register_policy(int policy, sched_policy_impl_t *impl) {
  kassert(policy < NUM_POLICIES);
  policy_impls[policy] = impl;
  return 0;
}


void sched_init() {
  sched_t *sched = kmallocz(sizeof(sched_t));
  sched->cpu_id = PERCPU_ID;
  spin_init(&sched->lock);

  // initialize scheduler policies
  for (int i = 0; i < NUM_POLICIES; i++) {
    sched->policies[i].data = POLICY_FUNC(i, init)(sched);
    spin_init(&sched->policies[i].lock);
  }

  // sched->idle_td =
  PERCPU_SET_SCHED(sched);
}

//

noreturn void sched_idle_thread() {
  sched_t *sched = PERCPU_SCHED;

  clock_t expires = clock_future_time(MS_TO_NS(1000));
  while (true) {

  }
}

// noreturn void sched_init() {
//   static bool done = false;
//   process_t *root = process_get(0);
//   if (!PERCPU_IS_BSP) {
//     // AP processors need to wait for BSP to finish
//     kprintf("sched: CPU#%d waiting\n", PERCPU_ID);
//     while (!done) cpu_pause();
//   }
//
//   id_t tid = atomic_fetch_add(&root->num_threads, 1);
//   str_t idle_name = str_from_charp(kasprintf("idle.%d", PERCPU_ID));
//   thread_t *idle = thread_alloc(tid, sched_idle_thread, NULL, idle_name, false);
//   idle->process = root;
//   idle->priority = 255;
//   idle->policy = 255;
//   LOCK(root);
//   LIST_ADD(&root->threads, idle, group);
//   UNLOCK(root);
//
//   sched_t *sched = kmalloc(sizeof(sched_t));
//   sched->cpu_id = PERCPU_ID;
//   spin_init(&sched->lock);
//
//   sched->ready_count = 0;
//   sched->blocked_count = 0;
//   sched->total_count = PERCPU_IS_BSP ? 1 : 0;
//   sched->idle_time = 0;
//
//   sched->active = PERCPU_IS_BSP ? root->main : idle;
//   sched->idle = idle;
//
//   LIST_INIT(&sched->blocked);
//
//   foreach_policy(policy) {
//     sched->policies[policy].data = NULL;
//     spin_init(&sched->policies[policy].lock);
//   }
//
//   PERCPU_SET_SCHED(sched);
//   _schedulers[sched->cpu_id] = sched;
//   atomic_fetch_add(&_num_schedulers, 1);
//
//   // register policies
//   register_policy(POLICY_SYSTEM, &sched_policy_fprr);
//   register_policy(POLICY_DRIVER, &sched_policy_fprr);
//
//   init_oneshot_timer();
//   timer_enable(TIMER_ONE_SHOT);
//   if (PERCPU_IS_BSP) {
//     // schedule the root main thread onto the primary core
//     thread_t *root_main = root->main;
//     SCHED_DISPATCH(sched, root_main->policy, policy_init_thread, root_main);
//     sched->total_count++;
//     sched_add_ready_thread(sched, root_main);
//     done = true;
//   }
//
//   kprintf("sched: CPU#%d initialized\n", PERCPU_ID);
//
//   if (PERCPU_IS_BSP) {
//     // wait until all schedulers are initialized
//     while (_num_schedulers != system_num_cpus) {
//       cpu_pause();
//     }
//   }
//
//   PERCPU_SET_THREAD(NULL);
//   sched_reschedule(SCHED_UPDATED);
//   unreachable;
// }

int sched_add(thread_t *td) {
  ASSERT(td->state == TDS_READY);
  ASSERT(td->policy < NUM_POLICIES);
  sched_t *sched = select_cpu_sched_for_thread(td);

  DPRINTF("adding thread %d:%d [{:str}] to CPU#%d\n", td->process->pid, td->tid, sched->cpu_id);
  SCHED_LOCK(sched);
  POLICY_DISPATCH(sched, td->policy, policy_init_thread, td);
  sched_add_ready_thread(sched, td);

  if (sched_should_preempt(sched, td)) {
    sched_do_reschedule(sched, SCHR_PREEMPTED);
  }

  SCHED_UNLOCK(sched);

  todo();
}

int reschedule(sched_reason_t reason) {
  todo();

  // uint64_t flags;
  // temp_irq_save(flags);

}

// int sched_add(thread_t *thread) {
//   sched_assert(thread->stats != NULL);
//   sched_assert(thread->policy < NUM_POLICIES);
//   sched_t *sched = sched_find_cpu_for_thread(thread);
//
//   kprintf("[CPU#%d] sched: adding thread %d.%d [%s] to CPU#%d\n",
//           PERCPU_ID, thread->process->pid, thread->tid, thread->name, sched->cpu_id);
//
//   uint64_t flags;
//   temp_irq_save(flags);
//   LOCK_SCHED(sched);
//   LOCK_THREAD(thread);
//   LOCK_POLICY(sched, thread);
//
//   thread->cpu_id = sched->cpu_id;
//   thread->status = THREAD_READY;
//   SCHED_DISPATCH(sched, thread->policy, policy_init_thread, thread);
//   sched->total_count++;
//   sched_add_ready_thread(sched, thread);
//
//   UNLOCK_POLICY(sched, thread);
//   UNLOCK_THREAD(thread);
//   UNLOCK_SCHED(sched);
//   temp_irq_restore(flags);
//
//   if (sched_should_preempt(sched, thread)) {
//     if (thread->cpu_id == PERCPU_ID) {
//       // reschedule current cpu
//       return sched_reschedule(SCHED_PREEMPTED);
//     }
//
//     DPRINTF("[CPU#%d] sched: sending ipi to CPU#%d\n", PERCPU_ID, thread->cpu_id);
//     return ipi_deliver_cpu_id(IPI_SCHEDULE, thread->cpu_id, SCHED_PREEMPTED);
//   }
//   return 0;
// }

// int sched_terminate(thread_t *thread) {
//   sched_assert(thread->status != THREAD_TERMINATED);
//   sched_t *sched = SCHEDULER(thread->cpu_id);
//
//   if (thread->status == THREAD_RUNNING) {
//     if (thread->cpu_id == PERCPU_ID) {
//       // reschedule current cpu and let the scheduler clean up on next pass
//       return sched_reschedule(SCHED_TERMINATED);
//     }
//     return ipi_deliver_cpu_id(IPI_SCHEDULE, thread->cpu_id, SCHED_TERMINATED);
//   }
//
//   uint64_t flags;
//   temp_irq_save(flags);
//   LOCK_SCHED(sched);
//   LOCK_THREAD(thread);
//   LOCK_POLICY(sched, thread);
//
//   if (thread->status == THREAD_READY) {
//     sched_remove_ready_thread(sched, thread);
//   } else if (thread->status == THREAD_BLOCKED) {
//     sched_remove_blocked_thread(sched, thread);
//   } else if (thread->status == THREAD_SLEEPING) {
//     // TODO: support canceling a timer on another cpu
//     sched_assert(thread->cpu_id == PERCPU_ID);
//     timer_delete_alarm(thread->alarm_id);
//     sched_remove_blocked_thread(sched, thread);
//   } else {
//     panic("sched_terminate: not implemented");
//   }
//
//   thread->status = THREAD_TERMINATED;
//   sched->total_count--;
//   SCHED_DISPATCH(sched, thread->policy, policy_deinit_thread, thread);
//
//   UNLOCK_POLICY(sched, thread);
//   UNLOCK_THREAD(thread);
//   UNLOCK_SCHED(sched);
//   temp_irq_restore(flags);
//   return 0;
// }
//
// int sched_block(thread_t *thread) {
//   sched_t *sched = SCHEDULER(thread->cpu_id);
//   sched_assert(!IS_BLOCKED(thread));
//
//   DPRINTF("[CPU#%d] sched: blocking thread %d.%d [%s] on CPU#%d\n",
//           PERCPU_ID, thread->process->pid, thread->tid, thread->name, sched->cpu_id);
//   if (thread->status == THREAD_RUNNING) {
//     if (thread->cpu_id == PERCPU_ID) {
//       // reschedule current cpu
//       return sched_reschedule(SCHED_BLOCKED);
//     }
//
//     DPRINTF("[CPU#%d] sched: sending ipi to CPU#%d\n", PERCPU_ID, thread->cpu_id);
//     return ipi_deliver_cpu_id(IPI_SCHEDULE, thread->cpu_id, SCHED_BLOCKED);
//   }
//
//   uint64_t flags;
//   temp_irq_save(flags);
//   LOCK_SCHED(sched);
//   LOCK_THREAD(thread);
//   LOCK_POLICY(sched, thread);
//
//   sched_assert(thread->status == THREAD_READY);
//   sched_remove_ready_thread(sched, thread);
//   thread->status = THREAD_BLOCKED;
//   sched_add_blocked_thread(sched, thread);
//
//   UNLOCK_POLICY(sched, thread);
//   UNLOCK_THREAD(thread);
//   UNLOCK_SCHED(sched);
//   temp_irq_restore(flags);
//   return 0;
// }
//
// int sched_unblock(thread_t *thread) {
//   sched_t *sched = SCHEDULER(thread->cpu_id);
//   if (!IS_BLOCKED(thread)) {
//     panic("thread %d.%d not blocked [%s]", thread->tid, thread->process->pid, thread->name);
//   }
//
//   DPRINTF("[CPU#%d] sched: unblocking thread %d.%d [%s] on CPU#%d\n",
//           PERCPU_ID, thread->process->pid, thread->tid, thread->name, sched->cpu_id);
//
//   uint64_t flags;
//   temp_irq_save(flags);
//   LOCK_SCHED(sched);
//   LOCK_THREAD(thread);
//   LOCK_POLICY(sched, thread);
//
//   sched_remove_blocked_thread(sched, thread);
//   thread->status = THREAD_READY;
//   sched_add_ready_thread(sched, thread);
//
//   UNLOCK_POLICY(sched, thread);
//   UNLOCK_THREAD(thread);
//   UNLOCK_SCHED(sched);
//   temp_irq_restore(flags);
//
//   if (sched_should_preempt(sched, thread)) {
//     if (thread->cpu_id == PERCPU_ID) {
//       // reschedule current cpu
//       return sched_reschedule(SCHED_PREEMPTED);
//     }
//
//     DPRINTF("[CPU#%d] sched: sending ipi to CPU#%d\n", PERCPU_ID, thread->cpu_id);
//     return ipi_deliver_cpu_id(IPI_SCHEDULE, thread->cpu_id, SCHED_PREEMPTED);
//   }
//   return 0;
// }
//
// int sched_wakeup(thread_t *thread) {
//   sched_t *sched = SCHEDULER(thread->cpu_id);
//   sched_assert(thread->status == THREAD_SLEEPING);
//
//   DPRINTF("[CPU#%d] sched: waking up thread %d.%d [%s] on CPU#%d\n",
//           PERCPU_ID, thread->process->pid, thread->tid, thread->name, thread->cpu_id);
//
//   uint64_t flags;
//   temp_irq_save(flags);
//   LOCK_SCHED(sched);
//   LOCK_THREAD(thread);
//   LOCK_POLICY(sched, thread);
//
//   sched_remove_blocked_thread(sched, thread);
//   thread->status = THREAD_READY;
//   sched_add_ready_thread(sched, thread);
//
//   UNLOCK_POLICY(sched, thread);
//   UNLOCK_THREAD(thread);
//   UNLOCK_SCHED(sched);
//   temp_irq_restore(flags);
//
//   if (sched_should_preempt(sched, thread)) {
//     if (thread->cpu_id == PERCPU_ID) {
//       // reschedule current cpu
//       return sched_reschedule(SCHED_PREEMPTED);
//     }
//     // rescheduler another cpu
//
//     DPRINTF("[CPU#%d] sched: sending ipi to CPU#%d\n", PERCPU_ID, thread->cpu_id);
//     return ipi_deliver_cpu_id(IPI_SCHEDULE, thread->cpu_id, SCHED_PREEMPTED);
//   }
//   return 0;
// }
//
// int sched_setsched(sched_opts_t opts) {
//   thread_t *thread = PERCPU_THREAD;
//   sched_t *sched = SCHEDULER(thread->cpu_id);
//
//   uint64_t flags;
//   temp_irq_save(flags);
//   LOCK_SCHED(sched);
//   LOCK_THREAD(thread);
//   LOCK_POLICY(sched, thread);
//
//   if (opts.policy != thread->policy) {
//     SCHED_DISPATCH(sched, thread->policy, policy_deinit_thread, thread);
//     thread->policy = opts.policy;
//     SCHED_DISPATCH(sched, thread->policy, policy_init_thread, thread);
//   }
//   if (opts.priority != thread->priority) {
//     thread->priority = opts.priority;
//   }
//   if (opts.affinity != thread->affinity) {
//     DPRINTF("[CPU#%d] sched: changing affinity of thread %d.%d [%s] to %d\n",
//             PERCPU_ID, thread->process->pid, thread->tid, thread->name, opts.affinity);
//     kassert(opts.affinity < system_num_cpus);
//     thread->affinity = opts.affinity;
//     if (opts.affinity >= 0 && opts.affinity != PERCPU_ID) {
//       UNLOCK_POLICY(sched, thread);
//       UNLOCK_THREAD(thread);
//       UNLOCK_SCHED(sched);
//       temp_irq_restore(flags);
//       return sched_reschedule(SCHED_UPDATED);
//     }
//   }
//
//   UNLOCK_POLICY(sched, thread);
//   UNLOCK_THREAD(thread);
//   UNLOCK_SCHED(sched);
//   temp_irq_restore(flags);
//   return 0;
// }
//
// int sched_sleep(uint64_t ns) {
//   thread_t *thread = PERCPU_THREAD;
//   sched_assert(thread->status == THREAD_RUNNING);
//
//   DPRINTF("[CPU#%d] sched: sleeping thread %d.%d [%s]\n",
//           PERCPU_ID, process_getpid(), process_gettid(), thread->name, thread->cpu_id);
//
//   clock_t now = clock_now();
//   thread->alarm_id = timer_create_alarm(now + ns, (void *) sched_wakeup, thread);
//   if (thread->alarm_id < 0) {
//     panic("failed to create alarm\n");
//   }
//   return sched_reschedule(SCHED_SLEEPING);
// }
//
// int sched_yield() {
//   thread_t *thread = PERCPU_THREAD;
//   sched_assert(thread->status == THREAD_RUNNING);
//
//   DPRINTF("[CPU#%d] sched: yielding thread %d.%d [%s]\n",
//           PERCPU_ID, process_getpid(), process_gettid(), thread->name, thread->cpu_id);
//
//   return sched_reschedule(SCHED_YIELDED);
// }
//
// //
//
// int sched_reschedule(sched_cause_t reason) {
//   DPRINTF("[CPU#%d] sched: rescheduling [%s]\n", PERCPU_ID, sched_reason_str[reason]);
//
//   uint64_t flags;
//   temp_irq_save(flags);
//
//   sched_assert(reason <= SCHED_YIELDED);
//   sched_t *sched = PERCPU_SCHED;
//   thread_t *curr = PERCPU_THREAD;
//   if (curr == NULL) {
//     // first time skip unlocks
//     LOCK_SCHED(sched);
//     goto next_thread_first;
//   }
//
//   // --------------
//
//   sched_assert(curr->cpu_id == sched->cpu_id);
//   if (reason == SCHED_PREEMPTED && curr->preempt_count > 0) {
//     curr->preempt_count--;
//     goto end;
//   } else if (reason == SCHED_PREEMPTED && sched->ready_count == 0) {
//     goto end;
//   }
//
//   LOCK_THREAD(curr);
//   sched_update_thread_time_end(sched, curr);
//   curr->status = get_thread_status(reason);
//   sched_update_thread_stats(sched, curr, reason);
//
//   if (reason == SCHED_UPDATED) {
//     sched_assert(curr != sched->idle);
//     if (curr->affinity >= 0 && curr->affinity != PERCPU_ID) {
//       sched_assert(curr->affinity < _num_schedulers);
//       sched_t *new_sched = SCHEDULER(curr->affinity);
//       sched_migrate_thread(sched, new_sched, curr);
//       LOCK_SCHED(sched);
//       goto next_thread;
//     }
//   }
//
//   LOCK_SCHED(sched);
//   if (curr != sched->idle)
//     LOCK_POLICY(sched, curr);
//
//   if (curr == sched->idle) {
//     curr->status = THREAD_READY;
//     sched->idle_time += curr->stats->last_active - curr->stats->last_scheduled;
//   } else if (IS_BLOCKED(curr)) {
//     sched_add_blocked_thread(sched, curr);
//   } else if (curr->status == THREAD_TERMINATED) {
//     sched->total_count--;
//     SCHED_DISPATCH(sched, curr->policy, policy_deinit_thread, curr);
//   } else {
//     sched_assert(curr->status == THREAD_READY);
//     sched_add_ready_thread(sched, curr);
//   }
//
//   if (curr != sched->idle)
//     UNLOCK_POLICY(sched, curr);
//
// LABEL(next_thread);
//   UNLOCK_THREAD(curr);
//
// LABEL(next_thread_first);
//   thread_t *next = sched_get_next_thread(sched);
//
//   LOCK_THREAD(next);
//   if (next != sched->idle)
//     LOCK_POLICY(sched, next);
//
//   next->status = THREAD_RUNNING;
//   sched->active = next;
//   sched_update_thread_time_start(sched, next);
//
//   if (next != sched->idle)
//     UNLOCK_POLICY(sched, next);
//   UNLOCK_THREAD(next);
//   UNLOCK_SCHED(sched);
//
//   if (next != curr) {
//     if (curr != NULL) {
//       DPRINTF("[CPU#%d] sched: switching from thread %d.%d [{:str}] to %d.%d [{:str}]\n",
//               PERCPU_ID,
//               curr->process->pid, curr->tid, &curr->name,
//               next->process->pid, next->tid, &next->name);
//     }
//
//     temp_irq_restore(flags);
//     thread_switch(next);
//     DPRINTF("[CPU#%d] sched: now in thread %d.%d [{:str}]\n",
//             PERCPU_ID, PERCPU_THREAD->process->pid, PERCPU_THREAD->tid, &PERCPU_THREAD->name);
//     return 0;
//   }
//
// LABEL(end);
//   temp_irq_restore(flags);
//   DPRINTF("[CPU#%d] sched: continuing in thread %d.%d [{:str}]\n",
//           PERCPU_ID, PERCPU_THREAD->process->pid, PERCPU_THREAD->tid, &PERCPU_THREAD->name);
//   return 0;
// }

//
