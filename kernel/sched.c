//
// Created by Aaron Gill-Braun on 2023-12-24.
//

#include <kernel/sched.h>

/**
 * A scheduler on a cpu.
 */
typedef struct sched {
  uint64_t id;                // scheduler id
  struct thread *idle;        // idle thread
  uint64_t readymask;         // runqueues with ready threads (bitmap)
  struct runqueue queues[64]; // runqueues (indexed by td->priority/4)
} sched_t;

//

void sched_init() {
  sched_t *sched = kmallocz(sizeof(sched_t));

}
