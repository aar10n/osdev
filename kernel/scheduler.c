//
// Created by Aaron Gill-Braun on 2020-10-18.
//

#include <scheduler.h>
#include <timer.h>
#include <percpu.h>
#include <panic.h>
#include <printf.h>
#include <vectors.h>
#include <spinlock.h>

#include <mm/heap.h>
#include <mm/vm.h>

#include <cpu/idt.h>
#include <device/apic.h>
#include <string.h>

#define sched_log(str, args...) kprintf("[scheduler] " str "\n", ##args)

// #define SCHED_DEBUG
#ifdef SCHED_DEBUG
#define sched_trace_debug(str, args...) kprintf("[scheduler] " str "\n", ##args)
#else
#define sched_trace_debug(str, args...)
#endif

#define DISPATCH(policy, func, args...) \
  (((SCHEDULER->policies[policy])->func)(SCHEDULER->policy_data[policy], ##args))
#define REGISTER_POLICY(policy, impl) ({           \
  SCHEDULER->policies[policy] = impl;              \
  SCHEDULER->policy_data[policy] = (impl)->init(); \
})

#define IS_BLOCKED(thread) \
  ((thread)->status == THREAD_BLOCKED || (thread)->status == THREAD_SLEEPING)

#define IS_TERMINATED(thread) ((thread)->status == THREAD_TERMINATED)

extern void tick_handler();
void thread_switch(thread_t *thread);

static const char *status_str[] = {
  [THREAD_READY] = "THREAD_READY",
  [THREAD_RUNNING] = "THREAD_RUNNING",
  [THREAD_BLOCKED] = "THREAD_BLOCKED",
  [THREAD_SLEEPING] = "THREAD_SLEEPING",
  [THREAD_TERMINATED] = "THREAD_TERMINATED",
  [THREAD_KILLED] = "THREAD_KILLED"
};

static process_t *ptable[MAX_PROCS] = {};
static size_t ptable_size = 0;
static spinlock_t ptable_lock =  {
  .locked = 0,
  .locked_by = 0,
  .lock_count = 0,
};

static inline bool is_valid_thread(thread_t *thread) {
  return thread->ctx != NULL &&
    thread->process != NULL &&
    thread->policy < SCHED_POLICIES &&
    thread->status == THREAD_READY;
}

//
// Scheduling Policies
//

// fixed priority round-robin

void *fprr_init() {
  policy_fprr_t *fprr = kmalloc(sizeof(policy_fprr_t));
  memset(fprr, 0, sizeof(policy_fprr_t));
  return fprr;
}

int fprr_add_thread(void *self, thread_t *thread, sched_reason_t reason) {
  policy_fprr_t *fprr = self;
  uint8_t priority = max(thread->priority, FPRR_NUM_PRIORITIES - 1);
  thread->priority = priority;

  LIST_ADD(&fprr->queues[priority], thread, list);
  fprr->count++;
  SCHEDULER->count++;
  return 0;
}

int fprr_remove_thread(void *self, thread_t *thread) {
  policy_fprr_t *fprr = self;
  uint8_t priority = thread->priority;

  LIST_REMOVE(&fprr->queues[priority], thread, list);
  fprr->count--;
  SCHEDULER->count--;
  return 0;
}

uint64_t fprr_get_thread_count(void *self) {
  return ((policy_fprr_t *) self)->count;
}

thread_t *fprr_get_next_thread(void *self) {
  policy_fprr_t *fprr = self;
  if (fprr->count == 0) {
    return NULL;
  }

  for (int i = 0; i < FPRR_NUM_PRIORITIES; i++) {
    thread_t *thread = LIST_FIRST(&fprr->queues[i]);
    if (thread == NULL) {
      continue;
    }

    fprr_remove_thread(self, thread);
    return thread;
  }
  return NULL;
}

void fprr_update_self(void *self) {
  policy_fprr_t *fprr = self;
}


sched_policy_t policy_fprr = {
  .init = fprr_init,
  .add_thread = fprr_add_thread,
  .remove_thread = fprr_remove_thread,

  .get_thread_count = fprr_get_thread_count,
  .get_next_thread = fprr_get_next_thread,

  .update_self = fprr_update_self,

  .config = {
    .can_change_priority = true
  }
};


//
// Scheduling
//

noreturn void *idle_task(void *arg) {
  percpu_t *p = PERCPU;
  scheduler_t *sched = p->scheduler;
  while (true) {
    if (sched->count > 0) {
      scheduler_yield();
    }
    cpu_pause();
  }
}

static inline thread_status_t get_new_status(sched_reason_t reason) {
  switch (reason) {
    case BLOCKED: return THREAD_BLOCKED;
    case PREEMPTED: return THREAD_READY;
    case SLEEPING: return THREAD_SLEEPING;
    case TERMINATED: return THREAD_TERMINATED;
    case YIELDED: return THREAD_READY;
    default: unreachable;
  }
}

thread_t *get_next_thread() {
  sched_trace_debug("getting next thread");
  scheduler_t *sched = SCHEDULER;
  if (sched->count == 0) {
    return NULL;
  }

  label(find_thread);
  thread_t *thread = NULL;
  for (int i = 0; i < SCHED_POLICIES; i++) {
    thread = DISPATCH(i, get_next_thread);
    if (thread != NULL) break;
  }

  if (thread == NULL) {
    return NULL;
  }

  // clean up thread if it has been terminated
  if (thread->status == THREAD_TERMINATED) {
    sched_trace_debug("cleaning up thread");
    // do cleanup logic
    if (sched->count == 0) {
      return NULL;
    }
    goto find_thread;
  } else if (thread->status != THREAD_READY) {
    panic("[scheduler] next thread not ready");
  }

  return thread;
}

//

void scheduler_sched(sched_reason_t reason) {
  // cli();
  scheduler_t *sched = SCHEDULER;
  thread_t *curr = current_thread;

  if (reason == PREEMPTED && curr->preempt_count > 0) {
    if (sched->timer_event) {
      sched->timer_event = false;
      thread_switch(curr);
    }
    return;
  }

  uint64_t count = sched->count;

  // sched_log("schedule");
  sched_trace_debug("schedule");
  if (curr == sched->idle) {
    curr->status = THREAD_READY;
  } else {
    curr->status = get_new_status(reason);
    sched_trace_debug("cur status: %s (%d:%d)", status_str[curr->status], curr->process->pid, curr->tid);
    if (IS_BLOCKED(curr)) {
      if (!(curr->flags & F_THREAD_OWN_BLOCKQ)) {
        LIST_ADD(&sched->blocked, curr, list);
      }
    } else if (!IS_TERMINATED(curr)) {
      int result = DISPATCH(curr->policy, add_thread, curr, reason);
      if (result != 0) {
        panic("[scheduler] add_thread failed: %d", result);
      }
    }
  }

  if (count == 0) {
    // switch to idle thread
    sched->timer_event = false;
    sched_trace_debug("idling...");
    // curr->status = THREAD_READY;
    if (curr != sched->idle) {
      thread_switch(sched->idle);
    }
    return;
  }

  thread_t *next = get_next_thread();
  if (next == NULL) {
    panic("[scheduler] failed to get next thread");
  }

  sched_trace_debug("pid: %d | thread: %d", next->process->pid, next->tid);
  sched->timer_event = false;
  next->status = THREAD_RUNNING;
  next->cpu_id = sched->cpu_id;
  thread_switch(next);
}

__used void scheduler_tick() {
  scheduler_t *sched = SCHEDULER;
  sched->timer_event = true;
  kprintf("!!!tick!!!\n");
  sched_trace_debug("tick");
  if (current_thread == sched->idle && sched->count == 0) {
    // no other threads are available to run so return from the
    // interrupt handler and continue idling
    return;
  }
  // preempt the current thread
  scheduler_sched(PREEMPTED);
}

void scheduler_wakeup(thread_t *thread) {
  sched_trace_debug("wakeup (%d:%d)", thread->process->pid, thread->tid);
  scheduler_t *sched = SCHEDULER;
  LIST_REMOVE(&sched->blocked, thread, list);
  DISPATCH(thread->policy, add_thread, thread, RESERVED);
  thread->status = THREAD_READY;
  scheduler_sched(PREEMPTED);
}

//

void scheduler_init(process_t *root) {
  sched_trace_debug("initializing");

  thread_t *idle = thread_alloc(LIST_LAST(&root->threads)->tid + 1, idle_task, NULL, false);
  idle->process = root;
  idle->priority = 255;
  idle->policy = 255;

  LIST_ADD(&root->threads, idle, group);
  scheduler_t *sched = kmalloc(sizeof(scheduler_t));
  sched->cpu_id = PERCPU->id;
  sched->count = 0;
  sched->idle = idle;
  LIST_INIT(&sched->blocked);
  sched->timer_event = false;

  SCHEDULER = sched;

  REGISTER_POLICY(SCHED_DRIVER, &policy_fprr);
  REGISTER_POLICY(SCHED_SYSTEM, &policy_fprr);

  idt_gate_t gate = gate((uintptr_t) tick_handler, KERNEL_CS, 0, INTERRUPT_GATE, 0, 1);
  idt_set_gate(VECTOR_SCHED_TIMER, gate);

  // apic_init_periodic(SCHED_PERIOD);
  sched_trace_debug("done!");

  root->main->status = THREAD_RUNNING;
  thread_switch(root->main);
}

int scheduler_add(thread_t *thread) {
  process_t *process = thread->process;
  if (!is_valid_thread(thread)) {
    return EINVAL;
  }

  int result = DISPATCH(thread->policy, add_thread, thread, RESERVED);
  if (result != 0) {
    return result;
  }

  spin_lock(&ptable_lock);
  if (ptable_size == MAX_PROCS) {
    panic("[scheduler] process table is full");
  } else if (ptable[process->pid] == NULL) {
    ptable[process->pid] = process;
    ptable_size++;
  }
  spin_unlock(&ptable_lock);
  return 0;
}

int scheduler_remove(thread_t *thread) {
  scheduler_t *sched = SCHEDULER;

  if (thread == current_thread) {
    scheduler_sched(TERMINATED);
    unreachable;
  } else if (IS_BLOCKED(thread)) {
    LIST_REMOVE(&sched->blocked, thread, list);
  } else {
    DISPATCH(thread->policy, remove_thread, thread);
  }
  thread->status = THREAD_TERMINATED;
  return 0;
}

int scheduler_update(thread_t *thread, uint8_t policy, uint16_t priority) {
  scheduler_t *sched = SCHEDULER;
  sched_policy_t *pol = sched->policies[policy];
  if (!pol->config.can_change_priority && priority != thread->priority) {
    return ENOTSUP;
  }

  if (thread == current_thread || IS_BLOCKED(thread)) {
    thread->policy = policy;
    thread->priority = priority;
  } else {
    DISPATCH(thread->policy, remove_thread, thread);
    thread->policy = policy;
    thread->priority = priority;
    DISPATCH(thread->policy, add_thread, thread, RESERVED);
  }
  return 0;
}

int scheduler_block(thread_t *thread) {
  bool reschedule = false;
  if (thread->status == THREAD_RUNNING) {
    reschedule = true;
  } else if (thread->status == THREAD_READY) {
    DISPATCH(thread->policy, remove_thread, thread);
  } else {
    return EINVAL;
  }

  thread->status = THREAD_BLOCKED;
  if (reschedule) {
    scheduler_sched(BLOCKED);
  }
  return 0;
}

int scheduler_unblock(thread_t *thread) {
  if (thread->status != THREAD_BLOCKED) {
    return EINVAL;
  }

  scheduler_t *sched = SCHEDULER;
  if (!(thread->flags & F_THREAD_OWN_BLOCKQ)) {
    LIST_REMOVE(&sched->blocked, thread, list);
  } else {
    thread->flags ^= F_THREAD_OWN_BLOCKQ;
  }
  DISPATCH(thread->policy, add_thread, thread, RESERVED);
  thread->status = THREAD_READY;
  if (thread->policy == current_thread->policy && thread->priority > current_thread->priority) {
    scheduler_sched(PREEMPTED);
  }
  return 0;
}

int scheduler_yield() {
  sched_trace_debug("yielding");
  scheduler_sched(YIELDED);
  return 0;
}

int scheduler_sleep(uint64_t ns) {
  thread_t *thread = current_thread;
  create_timer(timer_now() + ns, (void *) scheduler_wakeup, thread);
  scheduler_sched(SLEEPING);
  return 0;
}

//

sched_policy_t *scheduler_get_policy(uint8_t policy) {
  if (policy > SCHED_POLICIES) {
    return NULL;
  }
  return SCHEDULER->policies[policy];
}

process_t *scheduler_get_process(pid_t pid) {
  if (pid >= MAX_PROCS || pid < 0) {
    return NULL;
  }
  return ptable[pid];
}

//

void preempt_disable() {
  current_thread->preempt_count++;
}

void preempt_enable() {
  current_thread->preempt_count--;
}
