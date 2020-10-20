//
// Created by Aaron Gill-Braun on 2020-10-18.
//

#ifndef KERNEL_SCHEDULER_H
#define KERNEL_SCHEDULER_H

#include <base.h>
#include <process.h>
#include <lock.h>

#define SCHEDULER (PERCPU->scheduler)
#define INITIAL_QUANTUM 2

// Policies
#define POLICY_MLFQ 0

// --- Multilevel Feedback Queue ---
#define MLFQ_LEVELS 3   // the number of levels (queues)
#define MLFQ_ROLLOVER 4 // the rollover value
// the formua for the quantum of a given level
#define MLFQ_QUANTUM(X) (((X) * 2) + 1)

#define MLFQ_STATUS_NONE     0
#define MLFQ_STATUS_FINISHED 1
#define MLFQ_STATUS_YIELDED  2


//

typedef struct {
  size_t count;
  process_t *head;
  process_t *tail;
  spinlock_t lock;
} runqueue_t;

// Scheduling Policies

typedef struct {
  uint8_t level;
  uint64_t quantum;
  runqueue_t *queue;
} mlfq_queue_t;

typedef struct {
  uint8_t id;
  uint8_t status;
  uint64_t process_count;
  mlfq_queue_t levels[MLFQ_LEVELS];
} mlfq_t;

//

typedef struct {
  uint8_t id;       // policy id
  uint8_t penalty;  // the current penalty
  uint8_t rollover; // the max penalty before rollover
  void *self;       // policy specific data

  void *(*init)(uint8_t id);
  void (*enqueue)(void *self, process_t *process);
  // core scheduling
  process_t *(*schedule)(void *self);
  void (*cleanup)(void *self, process_t *process);
  void (*preempt)(void *self, process_t *process);
  void (*yield)(void *self, process_t *process);
  // debugging
  void (*print_stats)(void *self);
} schedl_policy_t;
#define SCHEDULER_POLICY(ID, RO, PREFIX)            \
  ((schedl_policy_t){                               \
    .id = ID, .self = NULL,                         \
    .penalty = 0,                                   \
    .rollover = RO,                                 \
    .init = (void *)(PREFIX ## _init),              \
    .enqueue = (void *)(PREFIX ## _enqueue),        \
                                                    \
    .schedule = (void *)(PREFIX ## _schedule),      \
    .cleanup = (void *)(PREFIX ## _cleanup),        \
    .preempt = (void *)(PREFIX ## _preempt),        \
    .yield = (void *)(PREFIX ## _yield),            \
                                                    \
    .print_stats = (void *)(PREFIX ## _print_stats) \
  })

typedef struct {
  uint8_t cpu;
  clock_t runtime;
  clock_t start_time;
  clock_t last_dispatch;
  size_t num_policies;
  schedl_policy_t *policies;
} schedl_entity_t;

void schedl_init();
void schedule(process_t *process);
void yield();

void schedl_print_stats();

#endif
