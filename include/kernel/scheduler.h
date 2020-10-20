//
// Created by Aaron Gill-Braun on 2020-10-18.
//

#ifndef KERNEL_SCHEDULER_H
#define KERNEL_SCHEDULER_H

#include <base.h>
#include <process.h>
#include <lock.h>

#define FREQUENCY 1    // scheduler frequency
#define MAX_PRIORITY 3 // number of priority levels

typedef struct {
  uint8_t priority;
  size_t count;
  process_t *head;
  process_t *tail;
  spinlock_t lock;
} runqueue_t;

typedef struct {
  uint64_t cpu_id;
  runqueue_t *queue[MAX_PRIORITY];
  volatile clock_t ticks;
} schedl_t;

void schedl_init();
void schedl_schedule(process_t *process);

#endif
