//
// Created by Aaron Gill-Braun on 2020-10-18.
//

#include <scheduler.h>
#include <timer.h>
#include <percpu.h>
#include <panic.h>
#include <lock.h>
#include <printf.h>
#include <vectors.h>

#include <mm/heap.h>
#include <mm/vm.h>

#include <cpu/idt.h>
#include <device/apic.h>
#include <string.h>

extern void tick_handler();
extern void switch_context(process_t *process, vm_t *vm);

static process_t *ptable[PTABLE_SIZE] = {};
static size_t ptable_size = 0;

//
// Runqueue Management
//

rqueue_t *rq_create() {
  rqueue_t *rq = kmalloc(sizeof(rqueue_t));
  rq->count = 0;
  rq->front = NULL;
  rq->back = NULL;
  spin_init(&rq->lock);
  return rq;
}

void rq_enqueue(rqueue_t *rq, process_t *process) {
  lock(rq->lock);
  if (rq->count == 0) {
    rq->front = process;
    rq->back = process;
  } else {
    process->prev = rq->back;
    rq->back->next = process;
    rq->back = process;
  }
  rq->count++;
  unlock(rq->lock);
}

void rq_enqueue_front(rqueue_t *rq, process_t *process) {
  lock(rq->lock);
  if (rq->count == 0) {
    rq->front = process;
    rq->back = process;
  } else {
    rq->front->prev = process;
    process->next = rq->front;
    rq->front = process;
  }
  rq->count++;
  unlock(rq->lock);
}

process_t *rq_dequeue(rqueue_t *rq) {
  if (rq->count == 0) {
    return NULL;
  }

  lock(rq->lock);
  process_t *process = rq->front;
  if (rq->count == 1) {
    rq->front = NULL;
    rq->back = NULL;
  } else {
    rq->front->prev = NULL;
    rq->front = process->next;
  }
  process->next = NULL;
  process->prev = NULL;
  rq->count--;
  unlock(rq->lock);
  return process;
}

void rq_remove(rqueue_t *rq, process_t *process) {
  lock(rq->lock);
  if (process == rq->front) {
    process->next->prev = NULL;
    rq->front = process->next;
  } else if (process == rq->back) {
    process->prev->next = NULL;
    rq->back = process->prev;
  } else {
    process->next->prev = process->prev;
    process->prev->next = process->next;
  }
  process->next = NULL;
  process->prev = NULL;
  rq->count--;
  unlock(rq->lock);
}

//
// Core Scheduling
//

void sched_schedule() {
  clock_t now = timer_now();
  process_t *curr = current;
  kprintf("[sched] schedule\n");

  if (curr) {
    curr->stats.last_run_end = now;
    curr->stats.run_time += now - curr->stats.last_run_start;
  }

  process_t *process = NULL;
  rqueue_t *rq = NULL;
  for (int i = 0; i < SCHED_QUEUES; i++) {
    rq = SCHEDULER->queues[i];
    if (rq->count > 0) {
      process = rq_dequeue(rq);
      break;
    }
  }

  if (curr && curr->status == PROC_READY) {
    if (process) {
      rq = SCHEDULER->queues[curr->priority];
      rq_enqueue(rq, curr);
    } else {
      process = curr;
    }
  } else if (process == NULL) {
    // idle task
    kprintf("[sched] idling...\n");
    process = SCHEDULER->idle;
  }

  process->status = PROC_RUNNING;
  process->stats.run_count++;
  process->stats.idle_time += now - process->stats.last_run_end;
  process->stats.last_run_start = timer_now();

  kprintf("[sched] pid: %d\n", process->pid);
  switch_context(process, process->vm);
}

void sched_tick() {
  kprintf("[sched] tick\n");
  // whole time slice was used
  process_t *curr = current;
  if (curr) {
    uint8_t new_prior = min(curr->priority + 1, SCHED_QUEUES - 1);
    curr->priority = new_prior;
    curr->status = PROC_READY;
  }
  sched_schedule();
}

noreturn void sched_idle() {
  while (true) {
    cpu_hlt();
  }
}

void sched_preempt(process_t *process) {
  // only part of time slice was used
  process->status = PROC_READY;
  process_t *curr = current;
  rqueue_t *rq = SCHEDULER->queues[process->priority];
  if (process->priority < curr->priority) {
    kprintf("[sched] preempting with pid %llu\n", process->pid);
    rq_enqueue_front(rq, process);
    sched_schedule();
  } else {
    rq_enqueue(rq, process);
  }
}

void sched_unblock(process_t *process) {
  kprintf("[sched] pid %llu: unblock\n", process->pid);
  rq_remove(SCHEDULER->blocked, process);
  sched_preempt(process);
}

void sched_wakeup(process_t *process) {
  kprintf("[sched] pid %llu: wakeup\n", process->pid);
  sched_preempt(process);
}

//
// Scheduler API
//

void sched_init() {
  kprintf("[sched] initializing\n");
  scheduler_t *sched = kmalloc(sizeof(scheduler_t));
  sched->cpu_id = PERCPU->id;
  sched->idle = kthread_create_idle(sched_idle);
  sched->idle->priority = 255;
  sched->blocked = rq_create();
  for (int i = 0; i < SCHED_QUEUES; i++) {
    sched->queues[i] = rq_create();
  }
  SCHEDULER = sched;

  idt_gate_t gate = gate((uintptr_t) tick_handler, KERNEL_CS, 0, INTERRUPT_GATE, 0, 1);
  idt_set_gate(VECTOR_SCHED_TIMER, gate);

  apic_init_periodic(SCHED_QUANTUM);
  kprintf("[sched] done!\n");
}

process_t *sched_get_process(uint64_t pid) {
  if (pid > ptable_size) {
    return NULL;
  }
  return ptable[pid];
}

void sched_enqueue(process_t *process) {
  rqueue_t *rq = SCHEDULER->queues[process->priority];
  rq_enqueue(rq, process);
  ptable[process->pid] = process;
}

void sched_block() {
  kprintf("[sched] pid %llu: block\n", current->pid);
  process_t *curr = current;
  curr->status = PROC_BLOCKED;
  curr->stats.block_count++;
  rq_enqueue(SCHEDULER->blocked, curr);
  sched_schedule();
}

void sched_sleep(uint64_t ns) {
  kprintf("[sched] pid %llu: sleep\n", current->pid);
  process_t *curr = current;
  curr->status = PROC_SLEEPING;
  curr->stats.sleep_count++;
  curr->stats.sleep_time += ns;

  create_timer(timer_now() + ns, (void *) sched_wakeup, curr);
  sched_schedule();
}

void sched_yield() {
  kprintf("[sched] pid %llu: yield\n", current->pid);
  process_t *curr = current;
  curr->status = PROC_READY;
  curr->stats.yield_count++;
  sched_schedule();
}

void sched_terminate() {
  kprintf("[sched] pid %llu: terminate\n", current->pid);
  process_t *curr = current;
  curr->status = PROC_KILLED;
  ptable[curr->pid] = NULL;
  current = NULL;
  sched_schedule();
}

// Debugging

void sched_print_stats() {
  uint64_t rflags = cli_save();

  kprintf("===== scheduler stats =====\n");
  kprintf("current pid: %llu\n", current ? current->pid : -1);
  if (current) {
    print_debug_process(current);
  }

  rqueue_t *rq;
  process_t *process;
  for (int i = 0; i < SCHED_QUEUES; ++i) {
    kprintf("---- level %d ----\n", i);
    rq = SCHEDULER->queues[i];
    process = rq->front;
    while (process) {
      print_debug_process(process);
      process = process->next;
    }
  }

  kprintf("---- blocked ----\n");
  rq = SCHEDULER->blocked;
  process = rq->front;
  while (process) {
    print_debug_process(process);
    process = process->next;
  }

  sti_restore(rflags);
}
