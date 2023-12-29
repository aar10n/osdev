//
// Created by Aaron Gill-Braun on 2023-12-24.
//

#ifndef KERNEL_SCHED_H
#define KERNEL_SCHED_H

#include <kernel/base.h>
#include <kernel/proc.h>
#include <kernel/tqueue.h>

struct sched;

typedef enum sched_cause {
  SCHED_BLOCKED,
  SCHED_SLEEPING,
  SCHED_PREEMPTED,
  SCHED_UPDATED,
  SCHED_EXITED,
} sched_cause_t;

void sched_init();

// thread_t *


// #define SCHED_POLICY_FPRR 0 // fixed priority round robin
// #define   NUM_POLICIES 1
//
// #define SCHED_LOCK(sched) (spin_lock(&(sched)->lock))
// #define SCHED_UNLOCK(sched) (spin_unlock(&(sched)->lock))
//
// /**
//  * A scheduler for a cpu.
//  */
// typedef struct sched {
//   uint8_t cpu_id;       // id of the cpu the scheduler runs on
//   uint32_t flags;       // scheduler flags
//   thread_t *idle_td;    // scheduler idle thread
//
//   spinlock_t lock;      // scheduler spinlock
//   size_t ready_count;   // number of ready threads
//   thread_t *active_td;  // active thread pointer
//
//   struct sched_policy {
//     void *data;         // policy private data
//     spinlock_t lock;    // policy lock
//   } policies[NUM_POLICIES];
// } sched_t;
//
// /**
//  * The reason for a call to the scheduler.
//  */
// typedef enum sched_reason {
//   SCHR_PREEMPTED,   // thread was preempted
//   SCHR_BLOCKED,     // thread was blocked
//   SCHR_SLEEPING,    // thread went to sleep
//   SCHR_UPDATED,     // thread wants rescheduling
//   SCHR_YIELDED,     // thread voluntarily yielded
//   SCHR_TERMINATED,  // thread was terminated
// } sched_reason_t;
//
// /**
//  * A scheduling policy implementation.
//  *
//  * A scheduling policy is a set of functions that define how a scheduler should behave.
//  */
// typedef struct sched_policy_impl {
//   /* required */
//
//   /// Initializes the policy for a scheduler.
//   void *(*init)(struct sched *sched);
//   /// Called when a thread is added to the scheduler.
//   int (*add_thread)(void *self, thread_t *td);
//   /// Called when a thread is removed from the scheduler.
//   int (*remove_thread)(void *self, thread_t *td);
//   /// Called to get the next thread to run and remove it from the policy.
//   /// The thread should be returned with its lock held.
//   thread_t *(*get_next_thread)(void *self);
//
//   /* optional */
//
//   /// Called to initialize per-thread policy specific stat data.
//   int (*policy_init_thread)(void *self, thread_t *td);
//   /// Called to deinitialize per-thread policy specific stat data.
//   int (*policy_deinit_thread)(void *self, thread_t *td);
//
//   /// Called when a thread is about to begin a timeslice.
//   int (*on_thread_timeslice_start)(void *self, thread_t *td);
//   /// Called when a thread has finished a timeslice.
//   int (*on_thread_timeslice_end)(void *self, thread_t *td, enum sched_reason reason);
//   /// Called when a thread is migrating to a new cpu.
//   int (*on_thread_migrate_cpu)(void *self, thread_t *td, uint8_t new_cpu);
//
//   /* static (optional) */
//
//   /// Called to determine if a thread should preempt another with the same policy.
//   /// By default, a thread with a higher priority will preempt another.
//   bool (*should_thread_preempt_same_policy)(thread_t *active, thread_t *other);
// } sched_policy_impl_t;
//
// int sched_register_policy(int policy, sched_policy_impl_t *impl);
//
// void sched_init();
// // int sched_add(thread_t *thread);
// // int sched_terminate(thread_t *thread);
// // int sched_block(thread_t *thread);
// // int sched_unblock(thread_t *thread);
// // int sched_sleep(uint64_t ns);
// // int sched_yield();
//
// int reschedule(sched_reason_t reason);

#endif
