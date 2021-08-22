//
// Created by Aaron Gill-Braun on 2020-10-18.
//

#ifndef KERNEL_SCHEDULER_H
#define KERNEL_SCHEDULER_H

#include <base.h>
#include <process.h>
#include <thread.h>
#include <cpu/idt.h>

// Scheduling Classes
// ------------------
// Interrupt
// System
// Interactive
//

#define SCHEDULER (PERCPU->scheduler)
#define CURRENT (PERCPU->curr)

#include <queue.h>

#define SCHED_PERIOD   500
#define SCHED_POLICIES 2
#define SCHED_QUEUES   4

typedef enum {
  BLOCKED,
  PREEMPTED,
  RESERVED,
  SLEEPING,
  TERMINATED,
  YIELDED,
} sched_reason_t;

// scheduling policies
#define SCHED_DRIVER 0
#define SCHED_SYSTEM 1

typedef struct {
  void *(*init)();
  int (*add_thread)(void *self, thread_t *thread, sched_reason_t reason);
  int (*remove_thread)(void *self, thread_t *thread);

  uint64_t (*get_thread_count)(void *self);
  thread_t *(*get_next_thread)(void *self);

  void (*update_self)(void *self);

  // properties
  struct {
    bool can_change_priority;
  } config;
} sched_policy_t;

// fixed-priority round robin
#define FPRR_NUM_PRIORITIES 3

#define PRIORITY_HIGH   0
#define PRIORITY_MEDIUM 1
#define PRIORITY_LOW    2

typedef struct {
  uint64_t count;
  LIST_HEAD(thread_t) queues[3];
} policy_fprr_t;

// multi-level feedback queue
#define MLFQ_NUM_QUEUES  4

typedef struct {

} policy_mlfq_t;

//

typedef struct scheduler {
  uint64_t cpu_id;
  uint64_t count;
  thread_t *idle;

  LIST_HEAD(thread_t) blocked;

  sched_policy_t *policies[SCHED_POLICIES];
  void *policy_data[SCHED_POLICIES];

  bool timer_event;
} scheduler_t;


void scheduler_init(process_t *root);
int scheduler_add(thread_t *thread);
int scheduler_remove(thread_t *thread);
int scheduler_update(thread_t *thread, uint8_t policy, uint16_t priority);
int scheduler_block(thread_t *thread);
int scheduler_unblock(thread_t *thread);
int scheduler_yield();
int scheduler_sleep(uint64_t ns);

sched_policy_t *scheduler_get_policy(uint8_t policy);
process_t *scheduler_get_process(pid_t pid);

void preempt_disable();
void preempt_enable();

#endif
