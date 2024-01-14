//
// Created by Aaron Gill-Braun on 2023-12-24.
//

#ifndef KERNEL_SCHED_H
#define KERNEL_SCHED_H

#include <kernel/base.h>
#include <kernel/proc.h>
#include <kernel/tqueue.h>

typedef enum sched_reason {
  SCHED_UPDATED,
  SCHED_YIELDED,
  SCHED_BLOCKED,
  SCHED_SLEEPING,
  SCHED_PREEMPTED,
  SCHED_EXITED,
} sched_reason_t;

void sched_init();
void sched_submit_new_thread(thread_t *td);
void sched_remove_ready_thread(thread_t *td);

void sched_again(sched_reason_t reason);


#endif
