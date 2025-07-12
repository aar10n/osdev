//
// Created by Aaron Gill-Braun on 2023-12-24.
//

#include <kernel/sched.h>
#include <kernel/proc.h>
#include <kernel/clock.h>
#include <kernel/ipi.h>

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
void switch_thread(thread_t *curr, thread_t *next);

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
int select_cpu_for_thread(thread_t *td) {
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
      if (td2 != td && td2->cpu_id != -1) {
        cpu = td2->cpu_id;
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

static inline thread_t *sched_runq_get_next_thread(sched_t *sched, int i) {
  bool empty;
  thread_t *td = runq_next_thread(&sched->queues[i], &empty);
  if (empty) {
    // clear the readymask bit if the runqueue is empty
    atomic_fetch_and(&sched->readymask, ~(1 << i));
  }
  return td;
}

// returns the next thread to run on the current cpu. the thread will be
// locked and in the ready state on return.
static thread_t *sched_next_thread() {
  sched_t *sched = cursched;
  thread_t *td = NULL;

  bool empty;
  int i = 0;
  if (sched->readymask != 0 && (i = bit_ffs64(sched->readymask)) != -1) {
    td = sched_runq_get_next_thread(sched, i);
    if (td != NULL) {
      // clear the readymask bit
      atomic_fetch_xor(&sched->readymask, 1 << i);
      return td;
    }
  }

  // do a linear scan of the runqueues
  for (i = 0; i < NRUNQS; i++) {
    // get the runq count without acquiring the lock and then
    // optimistically try to get the next thread
    if (atomic_load_relaxed(&sched->queues[i].count) > 0) {
      td = sched_runq_get_next_thread(sched, i);
      if (td != NULL) {
        break;
      }
    }
  }

  if (td == NULL) {
    // if no threads available, run idle thread
    td = sched->idle;
    // handle case where current thread is idle but no new thread is available
    // in this case we dont want to re-lock the idle thread.
    if (td_lock_owner(td) == NULL)
      td_lock(td);
  }
  return td;
}

//

// the cleanup_queue provides a way to handle deferred thread cleanup
// after a thread has exited. this is nessecary because a thread cannot
// free itself while it is exiting. there is a per-scheduler cleanup
// queue that is serviced by the idle thread.
static LIST_HEAD(struct thread) td_cleanup_queue[MAX_CPUS];
static mtx_t td_cleanup_lock[MAX_CPUS];

static void add_to_cleanup_queue(thread_t *td) {
  DPRINTF("adding thread {:td} to cleanup queue\n", td);
  ASSERT(td->plist.prev == NULL && td->plist.next == NULL);
  if (td_lock_owner(td) != NULL) {
    // unlock the thread if it is locked
    td_unlock(td);
  }

  int cpu = curcpu_id;
  mtx_spin_lock(&td_cleanup_lock[cpu]);
  LIST_ADD(&td_cleanup_queue[cpu], td, plist);
  mtx_spin_unlock(&td_cleanup_lock[cpu]);
}

noreturn void idle_thread_entry() {
  sched_t *sched = cursched;
  typeof(td_cleanup_queue[0]) *cleanup_queue = &td_cleanup_queue[curcpu_id];
  mtx_t *cleanup_lock = &td_cleanup_lock[curcpu_id];

  kprintf("starting idle thread on CPU#%d\n", curcpu_id);
idle_wait:;
  struct spin_delay delay = new_spin_delay(SHORT_DELAY, MAX_RETRIES);
  for (;;) {
    if (LIST_FIRST(cleanup_queue) != NULL) {
      mtx_spin_lock(cleanup_lock);
      thread_t *td;
      while ((td = LIST_REMOVE_FIRST(cleanup_queue, plist))) {
        DPRINTF("idle: cleaning up exited thread {:td}\n", td);
        thread_free_exited(&td);
      }
      mtx_spin_unlock(cleanup_lock);
    }

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
  cpu_scheds[curcpu_id] = sched;
  mtx_init(&td_cleanup_lock[curcpu_id], MTX_SPIN, "td_cleanup_lock");
  LIST_INIT(&td_cleanup_queue[curcpu_id]);

  set_cursched(sched);
  if (curthread == NULL) {
    // have the APs switch to their idle threads
    thread_t *td = sched->idle;
    TD_SET_STATE(td, TDS_RUNNING);
    td->start_time = clock_micro_time();
    td->last_sched_ns = clock_get_nanos();

    switch_thread(NULL, td);
    unreachable;
  }
}

void sched_submit_new_thread(thread_t *td) {
  td_lock_assert(td, MA_OWNED);
  ASSERT(TDS_IS_EMPTY(td));

  int cpu = select_cpu_for_thread(td);
  sched_t *sched = cpu_scheds[cpu];
  ASSERT(sched != NULL);

  TD_SET_STATE(td, TDS_READY);
  td->flags2 |= TDF2_FIRSTTIME;
  td->cpu_id = cpu;

  int i = td->priority / 4;
  runq_add(&sched->queues[i], td);
  atomic_fetch_or(&sched->readymask, 1 << i);
}

void sched_submit_ready_thread(thread_t *td) {
  td_lock_assert(td, MA_OWNED);
  ASSERT(TDS_IS_READY(td));

  int cpu = td->cpu_id;
  if (cpu < 0) {
    // thread cpu was cleared, reselect a cpu
    cpu = select_cpu_for_thread(td);
    td->cpu_id = cpu;
  }

  sched_t *sched = cpu_scheds[cpu];
  ASSERT(sched != NULL);

  int i = td->priority / 4;
  runq_add(&sched->queues[i], td);
  atomic_fetch_or(&sched->readymask, 1 << i);
}

void sched_remove_ready_thread(thread_t *td) {
  td_lock_assert(td, MA_OWNED);
  ASSERT(TDS_IS_READY(td));

  ASSERT(td->cpu_id >= 0);
  sched_t *sched = cpu_scheds[td->cpu_id];
  ASSERT(sched != NULL);

  bool empty;
  int i = td->priority / 4;
  runq_remove(&sched->queues[i], td, &empty);
  if (empty) {
    atomic_fetch_and(&sched->readymask, ~(1 << i));
  }
}

//

void sched_again(sched_reason_t reason) {
  if (reason == SCHED_PREEMPTED && curcpu_is_interrupt) {
    // we are in an interrupt so defer the preemption to interrupt exit
    set_preempted(true);
    return;
  }

  thread_t *oldtd = curthread;
  if (mtx_owner(&oldtd->lock) == NULL) {
    // lock the thread if it hasnt been already
    td_lock(oldtd);
  }
  td_lock_assert(oldtd, MA_NOTRECURSED);

  // select next thread now
  thread_t *newtd = sched_next_thread();
  td_lock_assert(newtd, MA_OWNED);

  if (TDF_IS_IDLE(newtd)) {
    if (newtd == oldtd) {
      // idle thread tried to yield but no other thread is ready
      // so we just return to the idle thread
      td_unlock(oldtd); // only unlock once
      return;
    }

    // dont rereschedule to idle thread if oldtd is just yielding or being preempted
    // because we have nothing better to do
    if (reason == SCHED_PREEMPTED || reason == SCHED_YIELDED) {
      td_unlock(oldtd);
      td_unlock(newtd);
      return; // return to oldtd
    }
  }

  switch (reason) {
    case SCHED_PREEMPTED:
      ASSERT(!curcpu_is_interrupt);
      TD_SET_STATE(oldtd, TDS_READY);
      atomic_fetch_or(&oldtd->flags2, TDF2_TRAPFRAME);
      sched_submit_ready_thread(oldtd);
      break;
    case SCHED_YIELDED:
      TD_SET_STATE(oldtd, TDS_READY);
      if (!TDF2_IS_STOPPED(oldtd)) {
        // if the thread is yielding because it was
        // stopped do not add it back to the runqueue
        sched_submit_ready_thread(oldtd);
      }
      break;
    case SCHED_BLOCKED:
      TD_SET_STATE(oldtd, TDS_BLOCKED);
      if (oldtd->proc->pid != 0) // proc0 is allowed to block forever
        ASSERT(oldtd->contested_lock != NULL);
      break;
    case SCHED_SLEEPING:
      TD_SET_STATE(oldtd, TDS_WAITING);
      ASSERT(oldtd->wchan != NULL);
      break;
    case SCHED_EXITED:
      ASSERT(TDF2_IS_STOPPED(oldtd));
      TD_SET_STATE(oldtd, TDS_EXITED);
      atomic_fetch_add(&oldtd->proc->num_exited, 1);
      cond_broadcast(&oldtd->proc->td_exit_cond);
      add_to_cleanup_queue(oldtd);
      oldtd = NULL; // oldtd is no longer valid
      break;
    default:
      unreachable;
  }

  if (TDF2_IS_FIRSTTIME(newtd)) {
    newtd->start_time = clock_micro_time();
    newtd->last_sched_ns = clock_get_nanos();
    newtd->flags2 &= ~TDF2_FIRSTTIME;
    DPRINTF("thread {:td} started on CPU#%d\n", newtd, curcpu_id);
  } else {
    newtd->last_sched_ns = clock_get_nanos();
    DPRINTF("thread {:td} resumed on CPU#%d\n", newtd, curcpu_id);
  }

  TD_SET_STATE(newtd, TDS_RUNNING);
  td_unlock(newtd);
  switch_thread(oldtd, newtd); // oldtd is unlocked by switch_thread
}

void sched_cpu(int cpu, sched_reason_t reason) {
  ASSERT(cpu >= 0 && cpu < system_num_cpus);
  if (cpu == curcpu_id) {
    sched_again(reason);
  } else {
    if (ipi_deliver_cpu_id(IPI_SCHEDULE, (uint8_t)cpu, (uint64_t)reason) < 0) {
      panic("failed to deliver IPI_SCHEDULE to CPU#%d", cpu);
    }
  }
}
