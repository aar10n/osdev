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

extern void context_switch();

runqueue_t *rq_create(uint8_t priority) {
  runqueue_t *rq = kmalloc(sizeof(runqueue_t));
  rq->priority = priority;
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

__used process_t *schedl_tick() {
  kprintf("[schedl] tick\n");
  PERCPU->scheduler->ticks++;
  if (PERCPU->current) {
    PERCPU->current->runtime++;
  }

  for (int i = 0; i < MAX_PRIORITY; i++) {
    runqueue_t *queue = PERCPU->scheduler->queue[i];
    if (queue->count > 0) {
      process_t *process = rq_dequeue(queue);
      kprintf("[schedl] switching to pid %d\n", process->pid);
      return process;
    }
  }

  kprintf("[schedl] sticking with pid %d\n",
          PERCPU->current ? PERCPU->current->pid : -1);
  return PERCPU->current;
}

__used void schedl_cleanup(process_t *old_process) {
  runqueue_t *queue = PERCPU->scheduler->queue[old_process->priority];
  rq_enqueue(queue, old_process);
}

__used void schedl_log() {
  kprintf("[pid %d] stack: %p\n", PERCPU->current->pid, PERCPU->current->ctx->rsp);
}

//

void schedl_init() {
  kassert(PERCPU->scheduler == NULL);
  kprintf("[schedl] initializing\n");

  schedl_t *schedl = kmalloc(sizeof(schedl_t));
  schedl->cpu_id = PERCPU->id;
  schedl->ticks = 0;
  for (int i = 0; i < MAX_PRIORITY; i++) {
    runqueue_t *rq = rq_create(i);
    schedl->queue[i] = rq;
  }

  PERCPU->current = NULL;
  PERCPU->scheduler = schedl;

  idt_gate_t timer_gate = gate((uintptr_t)context_switch, KERNEL_CS, 0, INTERRUPT_GATE, 0, 1);
  idt_set_gate(VECTOR_APIC_TIMER, timer_gate);
  apic_init_timer(FREQUENCY);

  kprintf("[schedl] done\n");
}

void schedl_schedule(process_t *process) {
  kprintf("[schedl] scheduling process (pid %u)\n", process->pid);
  runqueue_t *queue = PERCPU->scheduler->queue[process->priority];
  rq_enqueue(queue, process);
}
