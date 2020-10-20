//
// Created by Aaron Gill-Braun on 2020-10-18.
//

#include <scheduler.h>
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

runqueue_t *rq_create() {
  runqueue_t *rq = kmalloc(sizeof(runqueue_t));
  rq->count = 0;
  rq->head = NULL;
  rq->tail = NULL;
  spin_init(&rq->lock);
  return rq;
}

void rq_enqueue(runqueue_t *rq, process_t *process) {
  spin_lock(&rq->lock);
  // ----------------
  if (rq->count == 0) {
    rq->head = process;
    rq->tail = process;
  } else {
    rq->tail->next = process;
    rq->tail = process;
  }
  rq->count++;
  process->next = NULL;
  // ----------------
  spin_unlock(&rq->lock);
}

process_t *rq_dequeue(runqueue_t *rq) {
  if (rq->count == 0) {
    return NULL;
  }

  spin_lock(&rq->lock);
  // ----------------
  process_t *process = rq->head;
  if (rq->count == 1) {
    rq->head = NULL;
    rq->tail = NULL;
  } else {
    rq->head = rq->head->next;
  }
  rq->count--;
  process->next = NULL;
  // ----------------
  spin_unlock(&rq->lock);
  return process;
}

//
// Multilevel Feedback Queue
//

mlfq_t *mlfq_init(uint8_t id) {
  mlfq_t *self = kmalloc(sizeof(mlfq_t));
  self->id = id;
  self->status = MLFQ_STATUS_NONE;
  self->process_count = 0;
  for (int i = 0; i < MLFQ_LEVELS; i++) {
    mlfq_queue_t *level = &(self->levels[i]);
    level->level = i;
    level->quantum = MLFQ_QUANTUM(i);
    level->queue = rq_create();
  }

  return self;
}

void mlfq_enqueue(mlfq_t *self, process_t *process) {
  kassert(process->policy == self->id);
  process->priority = 0;
  mlfq_queue_t *level = &(self->levels[0]);
  rq_enqueue(level->queue, process);
  self->process_count++;
}

//

process_t *mlfq_schedule(mlfq_t *self) {
  // kprintf("[schedl] mlfq: schedule\n");
  if (self->process_count == 0) {
    return 0;
  }

  mlfq_queue_t *level = NULL;
  process_t *next = NULL;
  for (int i = 0; i < MLFQ_LEVELS; i++) {
    level = &(self->levels[i]);
    if (level->queue->count > 0) {
      next = rq_dequeue(level->queue);
      // kprintf("[schedl] mlfq: switching to pid %d\n", next->pid);
      break;
    }
  }

  if (!level || !next) {
    return NULL;
  }

  next->state = RUNNING;
  apic_oneshot(level->quantum);
  return next;
}

void mlfq_cleanup(mlfq_t *self, process_t *process) {
  // kprintf("[schedl] mlfq: cleanup\n");
  mlfq_queue_t *level = NULL;
  if (self->status == MLFQ_STATUS_YIELDED) {
    // same priority since the task yielded before the quantum ended
    level = &(self->levels[process->priority]);
  } else {
    // lower the priority since it used up the entire quantum
    uint8_t new_level = min(process->priority + 1, MLFQ_LEVELS - 1);
    level = &(self->levels[new_level]);
    process->priority = new_level;
  }

  process->state = READY;
  rq_enqueue(level->queue, process);
  self->status = MLFQ_STATUS_FINISHED;
}

void mlfq_preempt(mlfq_t *self, process_t *process) {
  kassert(process->policy == self->id);
  kprintf("[schedl] mlfq: preempt\n");

  // if the current process is from the same policy, only preempt if
  // it has a lower priority than the process. Otherwise, preempt it
  // if it is from any other policy.
  process_t *current = PERCPU->current;
  if (current->policy == self->id && current->priority >= process->priority) {
    // do not preempt
    mlfq_queue_t *level = &(self->levels[process->priority]);
    rq_enqueue(level->queue, process);
  } else {
    // preempt
    mlfq_queue_t *level = &(self->levels[0]);
    rq_enqueue(level->queue, process);
    self->status = MLFQ_STATUS_YIELDED;
    apic_oneshot(0);
  }
}

void mlfq_yield(mlfq_t *self, process_t *process) {
  // kprintf("[schedl] mlfq: yield (pid %d)\n", process->pid);
  self->status = MLFQ_STATUS_YIELDED;
  apic_interrupt(); // preempt the task
}

// Debugging

void mlfq_print_stats(mlfq_t *self) {
  kprintf("[schedl] mlfq: print stats\n");
  for (int i = 0; i < MLFQ_LEVELS; i++) {
    mlfq_queue_t *level = &(self->levels[i]);
    kprintf("--- level: %d ---\n", i);
    if (level->queue->count == 0) {
      continue;
    }

    process_t *process = level->queue->head;
    while (process) {
      print_debug_process(process);
      process = process->next;
    }
  }
}

//
// Core Scheduling
//

__used process_t *schedl_schedule() {
  // kprintf("[schedl] schedule\n");

  uint64_t last_quantum = read_tsc() - SCHEDULER->last_dispatch;
  SCHEDULER->runtime += last_quantum;
  if (PERCPU->current) {
    PERCPU->current->runtime += last_quantum;
  }

  for (int i = 0; i < SCHEDULER->num_policies; i++) {
    schedl_policy_t *policy = &(SCHEDULER->policies[i]);
    if (policy->penalty < policy->rollover) {
      policy->penalty++;
      process_t *next = policy->schedule(policy->self);
      if (next != NULL) {
        next->state = RUNNING;
        return next;
      }
    } else if (i == SCHEDULER->num_policies - 1) {
      // kprintf("[schedl] resetting penalties\n");
      // the last element had a rollover so reset all
      // penalties and reschedule again
      for (int j = 0; j < SCHEDULER->num_policies; j++) {
        SCHEDULER->policies[j].penalty = 0;
      }
      return schedl_schedule();
    }
  }
  return PERCPU->current;
}

__used void schedl_cleanup(process_t *process) {
  // kprintf("[schedl] cleanup\n");
  kassert(process->policy < SCHEDULER->num_policies);
  schedl_policy_t *policy = &(SCHEDULER->policies[process->policy]);
  process->state = READY;
  policy->cleanup(policy->self, process);
  SCHEDULER->last_dispatch = read_tsc();
}

//

static schedl_policy_t policies[] = {
  SCHEDULER_POLICY(POLICY_MLFQ, MLFQ_ROLLOVER, mlfq)
};
#define NUM_POLICIES (sizeof(policies) / sizeof(schedl_policy_t))

void schedl_init() {
  kassert(PERCPU->scheduler == NULL);
  kprintf("[schedl] initializing\n");

  schedl_entity_t *schedl = kmalloc(sizeof(schedl_entity_t));
  schedl->cpu = PERCPU->id;
  schedl->runtime = 0;
  schedl->num_policies = NUM_POLICIES;
  schedl->policies = kmalloc(sizeof(policies));
  memcpy(schedl->policies, policies, sizeof(policies));

  // call the `init` function of each policy
  for (int i = 0; i < NUM_POLICIES; i++) {
    schedl_policy_t *policy = &(schedl->policies[i]);
    policy->self = policy->init(policy->id);
  }

  PERCPU->current = NULL;
  PERCPU->scheduler = schedl;

  idt_gate_t timer_gate = gate((uintptr_t)context_switch, KERNEL_CS, 0, INTERRUPT_GATE, 0, 1);
  idt_set_gate(VECTOR_APIC_TIMER, timer_gate);
  kprintf("[schedl] done\n");
  apic_init_oneshot(INITIAL_QUANTUM);
  SCHEDULER->start_time = read_tsc();
  SCHEDULER->last_dispatch = read_tsc();
}

void schedule(process_t *process) {
  kassert(process->policy < NUM_POLICIES);
  kprintf("[schedl] scheduling process (pid %u)\n", process->pid, process->policy);

  schedl_policy_t *policy = &(SCHEDULER->policies[process->policy]);
  policy->enqueue(policy->self, process);
}

void yield() {
  process_t *process = PERCPU->current;
  kassert(process != NULL);
  kassert(process->policy < SCHEDULER->num_policies);
  schedl_policy_t *policy = &(SCHEDULER->policies[process->policy]);
  policy->yield(policy->self, process);
}

// Debugging

void schedl_print_stats() {
  kprintf("[schedl] print stats\n");
  kprintf("\n--- running process ---\n");
  print_debug_process(PERCPU->current);

  for (int i = 0; i < NUM_POLICIES; i++) {
    schedl_policy_t *policy = &(SCHEDULER->policies[i]);
    policy->print_stats(policy->self);
  }
}
