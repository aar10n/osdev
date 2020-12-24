//
// Created by Aaron Gill-Braun on 2020-10-18.
//

#ifndef KERNEL_SCHEDULER_H
#define KERNEL_SCHEDULER_H

#include <base.h>
#include <process.h>
#include <lock.h>

// Scheduling Classes
// ------------------
// Interrupt
// System
// Interactive
//

#define SCHEDULER (PERCPU->scheduler)
#define CURRENT (PERCPU->curr)

#define SCHED_QUANTUM 1000
#define PTABLE_SIZE 1024
#define SCHED_QUEUES  4

//

typedef struct {
  size_t count;
  process_t *front;
  process_t *back;
  spinlock_t lock;
} rqueue_t;

typedef struct scheduler {
  uint64_t cpu_id;
  process_t *idle;
  rqueue_t *queues[SCHED_QUEUES];
  rqueue_t *blocked;
} scheduler_t;


void sched_init();
process_t *sched_get_process(uint64_t pid);
void sched_enqueue(process_t *process);
void sched_schedule();
void sched_block();
void sched_sleep(uint64_t ns);
void sched_yield();
void sched_terminate();

void sched_print_stats();

#endif
