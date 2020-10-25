//
// Created by Aaron Gill-Braun on 2020-10-18.
//

#include <scheduler.h>
#include <timer.h>
#include <percpu.h>
#include <panic.h>
#include <lock.h>
#include <stdio.h>
#include <vectors.h>

#include <mm/heap.h>

#include <cpu/idt.h>
#include <device/apic.h>
#include <string.h>

extern void context_switch();
extern void tick_handler();
extern void switch_context(process_t *process);

static process_t **ptable = NULL;
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
  spin_lock(&rq->lock);
  // ----------------
  if (rq->count == 0) {
    rq->front = process;
    rq->back = process;
  } else {
    process->prev = rq->back;
    rq->back->next = process;
    rq->back = process;
  }
  rq->count++;
  // ----------------
  spin_unlock(&rq->lock);
}

void rq_enqueue_front(rqueue_t *rq, process_t *process) {
  spin_lock(&rq->lock);
  // ----------------
  if (rq->count == 0) {
    rq->front = process;
    rq->back = process;
  } else {
    rq->front->prev = process;
    process->next = rq->front;
    rq->front = process;
  }
  rq->count++;
  // ----------------
  spin_unlock(&rq->lock);
}

process_t *rq_dequeue(rqueue_t *rq) {
  if (rq->count == 0) {
    return NULL;
  }

  spin_lock(&rq->lock);
  // ----------------
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
  // ----------------
  spin_unlock(&rq->lock);
  return process;
}

void rq_remove(rqueue_t *rq, process_t *process) {
  spin_lock(&rq->lock);
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
  spin_unlock(&rq->lock);
}

//
// Core Scheduling
//

void sched_schedule() {
  clock_t now = timer_now();
  process_t *current = CURRENT;
  kprintf("[sched] schedule\n");

  if (current) {
    current->stats.last_run_end = now;
    current->stats.run_time += now - current->stats.last_run_start;
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

  if (current && current->status == PROC_READY) {
    if (process) {
      rq = SCHEDULER->queues[current->priority];
      rq_enqueue(rq, current);
    } else {
      process = current;
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
  switch_context(process);
}

void sched_tick() {
  kprintf("[sched] tick\n");
  // whole time slice was used
  process_t *current = CURRENT;
  if (current) {
    uint8_t new_prior = min(current->priority + 1, SCHED_QUEUES - 1);
    current->priority = new_prior;
    current->status = PROC_READY;
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
  process_t *current = CURRENT;
  rqueue_t *rq = SCHEDULER->queues[process->priority];
  if (process->priority < current->priority) {
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
  sched->idle = create_process(sched_idle);
  sched->blocked = rq_create();
  for (int i = 0; i < SCHED_QUEUES; i++) {
    sched->queues[i] = rq_create();
  }
  SCHEDULER = sched;

  idt_gate_t gate = gate((uintptr_t) tick_handler, KERNEL_CS, 0, INTERRUPT_GATE, 0, 1);
  idt_set_gate(VECTOR_APIC_TIMER, gate);

  if (ptable == NULL) {
    ptable = kmalloc(sizeof(process_t *) * PTABLE_SIZE);
    ptable_size = PTABLE_SIZE;
  }

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
  kprintf("[sched] pid %llu: block\n", CURRENT->pid);
  process_t *current = CURRENT;
  current->status = PROC_BLOCKED;
  current->stats.block_count++;
  rq_enqueue(SCHEDULER->blocked, current);
  sched_schedule();
}

void sched_sleep(uint64_t ns) {
  kprintf("[sched] pid %llu: sleep\n", CURRENT->pid);
  process_t *current = CURRENT;
  current->status = PROC_SLEEPING;
  current->stats.sleep_count++;
  current->stats.sleep_time += ns;

  create_timer(timer_now() + ns, (void *) sched_wakeup, current);
  sched_schedule();
}

void sched_yield() {
  kprintf("[sched] pid %llu: yield\n", CURRENT->pid);
  process_t *current = CURRENT;
  current->status = PROC_READY;
  current->stats.yield_count++;
  sched_schedule();
}

void sched_terminate() {
  kprintf("[sched] pid %llu: terminate\n", CURRENT->pid);
  process_t *current = CURRENT;
  current->status = PROC_KILLED;
  ptable[current->pid] = NULL;
  CURRENT = NULL;
  sched_schedule();
}
