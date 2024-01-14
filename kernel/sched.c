//
// Created by Aaron Gill-Braun on 2023-12-24.
//

#include <kernel/sched.h>
#include <kernel/proc.h>
#include <kernel/clock.h>

#include <kernel/panic.h>
#include <kernel/printf.h>
#include <kernel/atomic.h>
#include <kernel/bits.h>

#include <kernel/cpu/cpu.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(x, ...) kprintf("sched: " x, ##__VA_ARGS__)

#define NRUNQS 64

/*
 * A scheduler on a cpu.
 */
typedef struct sched {
  uint64_t id;                    // scheduler id
  volatile uint64_t readymask;    // runqueues with ready threads (bitmap)
  struct runqueue queues[NRUNQS]; // runqueues (indexed by td->priority/4)
  thread_t *idle;                 // idle thread
  uint64_t last_switch;           // last time a thread switch occured (ns)
} sched_t;
static sched_t *cpu_scheds[MAX_CPUS];

// defined in switch.asm
void sched_do_switch(thread_t *curr, thread_t *next);

// this function selects the cpu with the lowest thread count estimated by scanning
// the readymask of each cpu scheduler. note that the readymask only indicates which
// runqueues contain at least one thread, so this heuristic is not perfect but it is
// much faster than doing a full count of the runqueues.
static int select_cpu_by_lowest_rough_est_ready(int *out_est) {
  int cpu = -1;
  int min = INT_MAX;
  for (int i = 0; i < system_num_cpus; i++) {
    sched_t *sched = cpu_scheds[i];
    if (sched == NULL)
      continue;

    uint64_t mask = atomic_load_relaxed(&sched->readymask);
    if (mask == 0) {
      return i;
    }

    int n = bit_popcnt64(mask);
    if (n < min) {
      min = n;
      cpu = i;
    }
  }

  if (out_est != NULL)
    *out_est = min;
  return cpu;
}

// this function selects the cpu with the lowest thread count compared by summing
// the counts of each runqueue. this is a more accurate heuristic for sched load
// but it also requires more memory accesses.
static int select_cpu_by_lowest_readycnt(size_t *out_count) {
  int cpu = -1;
  size_t min = INT_MAX;
  for (int i = 0; i < system_num_cpus; i++) {
    sched_t *sched = cpu_scheds[i];
    if (sched == NULL)
      continue;

    size_t tdcount = 0;
    for (int j = 0; j < NRUNQS; j++) {
      tdcount += atomic_load_relaxed(&sched->queues[j].count);
    }

    if (tdcount < min) {
      min = tdcount;
      cpu = i;
    }
  }

  if (out_count != NULL)
    *out_count = min;
  return cpu;
}

// this function selects a cpu for a thread based on the thread's affinity,
// existing threads from the same process, and the current load on each cpu.
int select_cpu_for_new_thread(thread_t *td) {
  ASSERT(TDS_IS_EMPTY(td));
  proc_t *proc = td->proc;
  int cpu = -1;
  if (TDF2_HAS_AFFINITY(td)) {
    // TODO: select the most optimal cpu allowed by the mask
    if ((cpu = cpuset_next_set(td->cpuset, -1)) >= 0) {
      return cpu;
    }
  }

  if (TDF2_IS_FIRSTTIME(td) && PRF_HAS_RUN(proc)) {
    // some other thread(s) from this process have ran on a cpu
    // before so we can use the same one for this new thread
    pr_lock(proc);
    thread_t *td2 = LIST_FIRST(&proc->threads);
    while (td2 != NULL) {
      if (td2 != td && td2->last_cpu != -1) {
        cpu = td2->last_cpu;
        break;
      }
      td2 = LIST_NEXT(td2, plist);
    }
    pr_unlock(proc);
  }

  if (cpu >= 0)
    return cpu;

  // select cpu with lowest thread count
  return select_cpu_by_lowest_readycnt(NULL);
}

// returns the next thread to run on the current cpu. the thread will be
// locked and in the ready state on return.
static thread_t *sched_next_thread() {
  sched_t *sched = cursched;
  thread_t *td = NULL;

  int i = bit_ffs64(sched->readymask);
  if (i != -1) {
    td = runq_next_thread(&sched->queues[i]);
    if (td != NULL) {
      return td;
    }
  }

  // do a linear scan of the runqueues
  for (i = 0; i < NRUNQS; i++) {
    // get the runq count without acquiring the lock and then
    // optimistically try to get the next thread
    if (atomic_load_relaxed(&sched->queues[i].count) > 0) {
      td = runq_next_thread(&sched->queues[i]);
      if (td != NULL) {
        break;
      }
    }
  }

  if (td == NULL) {
    // if no threads available, run idle thread
    td = sched->idle;
    td_lock(td);
  }
  return td;
}

//

noreturn static void idle_thread(unused void *arg) {
  sched_t *sched = cursched;

idle_wait:;
  struct spin_delay delay = new_spin_delay(SHORT_DELAY, MAX_RETRIES);
  for (;;) {
    if (sched->readymask != 0) {
      // at least one of the runqueues could have a thread
      break;
    }

    if (!spin_delay_wait(&delay)) {
      // its been a while since there's been a ready thread, this could indicate a problem
      DPRINTF("idle thread on CPU#%d has been waiting for a while\n", curcpu_id);
      break;
    }
  }

  sched_again(SCHED_YIELDED);
  goto idle_wait;
}

//

void sched_init() {
  sched_t *sched = kmallocz(sizeof(sched_t));
  sched->id = curcpu_id;
  sched->idle = thread_alloc_idle();
  for (int i = 0; i < NRUNQS; i++) {
    runq_init(&sched->queues[i]);
  }

  set_cursched(sched);
  if (curthread == NULL) {
    // have the APs switch to their idle threads
    thread_t *td = sched->idle;
    TD_SET_STATE(td, TDS_RUNNING);
    td->start_time = clock_micro_time();

    sched_do_switch(NULL, td);
    unreachable;
  }
}

void sched_submit_new_thread(thread_t *td) {
  ASSERT(TDS_IS_EMPTY(td));
  td_lock_assert(td, MA_OWNED);

  int cpu = select_cpu_for_new_thread(td);
  sched_t *sched = cpu_scheds[cpu];
  ASSERT(sched != NULL);

  int i = td->priority / 4;
  struct runqueue *runq = &sched->queues[i];

  TD_SET_STATE(td, TDS_READY);
  td->flags2 |= TDF2_FIRSTTIME;
  td->last_cpu = cpu;

  runq_add(runq, td);
  atomic_fetch_or(&sched->readymask, 1 << i);
}

void sched_remove_ready_thread(thread_t *td) {
  ASSERT(TDS_IS_READY(td));
  td_lock_assert(td, MA_OWNED);


}

//

void sched_again(sched_reason_t reason) {
  thread_t *oldtd = curthread;
  if (mtx_owner(&oldtd->lock) == NULL) {
    // lock the thread if it hasnt been already
    mtx_lock(&oldtd->lock);
  }
  td_lock_assert(oldtd, MA_NOTRECURSED);

  // update thread state+stats
  switch (reason) {
    case SCHED_UPDATED:
      break;
    case SCHED_YIELDED:
      TD_SET_STATE(oldtd, TDS_READY);
      break;
    case SCHED_BLOCKED:
      TD_SET_STATE(oldtd, TDS_BLOCKED);
      break;
    case SCHED_SLEEPING:
      TD_SET_STATE(oldtd, TDS_WAITING);
      break;
    case SCHED_PREEMPTED:
      TD_SET_STATE(oldtd, TDS_READY);
      break;
    case SCHED_EXITED:
      ASSERT(TDF2_IS_STOPPING(oldtd));
      TD_SET_STATE(oldtd, TDS_EXITED);
      break;
  }

  thread_t *newtd = sched_next_thread();
  td_lock_assert(newtd, MA_OWNED);
  TD_SET_STATE(newtd, TDS_RUNNING);

  if (TDF2_IS_FIRSTTIME(newtd)) {
    newtd->start_time = clock_micro_time();

  }

  td_unlock(newtd);
  sched_do_switch(oldtd, newtd);

}

void sched_yield() {

}
