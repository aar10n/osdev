//
// Created by Aaron Gill-Braun on 2022-07-24.
//

#ifndef KERNEL_SCHED_SCHEDULER_H
#define KERNEL_SCHED_SCHEDULER_H

#include <base.h>
#include <queue.h>
#include <spinlock.h>

#define SCHED_COUNT_CACHE_AFFINITY_THRES  50

// scheduling policies
#define POLICY_SYSTEM 0
#define POLICY_DRIVER 1
#define NUM_POLICIES 2

typedef struct thread thread_t;
typedef struct process process_t;
typedef struct sched sched_t;

typedef enum sched_cause {
  SCHED_BLOCKED,        // current thread was blocked
  SCHED_PREEMPTED,      // current thread was preempted
  SCHED_SLEEPING,       // current thread is sleeping
  SCHED_TERMINATED,     // current thread was terminated
  SCHED_UPDATED,        // current thread has updated sched properties
  SCHED_YIELDED,        // current thread voluntarily yielded
} sched_cause_t;

typedef struct sched_policy_impl {
  void *(*init)(sched_t *sched); /* required */
  int (*add_thread)(void *self, thread_t *thread); /* required */
  int (*remove_thread)(void *self, thread_t *thread); /* required */
  thread_t *(*get_next_thread)(void *self); /* required */

  /* optional */
  int (*policy_init_thread)(void *self, thread_t *thread);
  int (*policy_deinit_thread)(void *self, thread_t *thread);

  int (*on_update_thread_stats)(void *self, thread_t *thread, sched_cause_t reason);
  int (*on_thread_timeslice_start)(void *self, thread_t *thread);
  int (*on_thread_timeslice_end)(void *self, thread_t *thread);
  int (*on_thread_migrate_cpu)(void *self, thread_t *thread, uint8_t new_cpu);

  /* static (optional) */
  bool (*should_thread_preempt_same_policy)(thread_t *active, thread_t *other);
  double (*compute_thread_cpu_affinity_score)(thread_t *thread);
} sched_policy_impl_t;

typedef struct sched_stats {
  clock_t total_time;
  clock_t last_active;
  clock_t last_scheduled;

  size_t sched_count;
  size_t preempt_count;
  size_t sleep_count;
  size_t yield_count;
  void *data;
} sched_stats_t;

typedef struct sched_opts {
  uint8_t policy;
  uint16_t priority;
  int affinity;
} sched_opts_t;

typedef struct sched {
  uint8_t cpu_id;       // id of the cpu the scheduler runs on
  spinlock_t lock;      // scheduler lock

  size_t ready_count;   // number of ready threads
  size_t blocked_count; // number of blocked or sleeping threads
  size_t total_count;   // total number of 'owned' threads
  clock_t idle_time;    // amount of time spent idling

  thread_t *active;     // active thread
  thread_t *idle;       // idle thread

  LIST_HEAD(thread_t) blocked;
  struct {
    void *data;
    spinlock_t lock;
  } policies[NUM_POLICIES];
} sched_t;

noreturn void sched_init(process_t *root);
int sched_add(thread_t *thread);
int sched_terminate(thread_t *thread);
int sched_block(thread_t *thread);
int sched_unblock(thread_t *thread);
int sched_wakeup(thread_t *thread);
int sched_setsched(sched_opts_t opts);
int sched_sleep(uint64_t ns);
int sched_yield();

int sched_reschedule(sched_cause_t reason);

#endif
