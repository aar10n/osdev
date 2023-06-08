//
// Created by Aaron Gill-Braun on 2022-07-24.
//

#include <sched/fprr.h>

#include <mm.h>
#include <thread.h>

#include <string.h>
#include <panic.h>

// Fixed Priority Round Robin Policy

typedef struct sched_policy_fprr {
  size_t count;
  LIST_HEAD(thread_t) queue;
} sched_policy_fprr_t;

void *fprr_init(sched_t *sched) {
  sched_policy_fprr_t *fprr = kmalloc(sizeof(sched_policy_fprr_t));
  fprr->count = 0;
  LIST_INIT(&fprr->queue);
  return fprr;
}

int fprr_add_thread(void *self, thread_t *thread) {
  sched_policy_fprr_t *fprr = self;
  LIST_ADD(&fprr->queue, thread, list);
  fprr->count++;
  return 0;
}

int fprr_remove_thread(void *self, thread_t *thread) {
  sched_policy_fprr_t *fprr = self;
  kassert(fprr->count > 0);
  LIST_REMOVE(&fprr->queue, thread, list);
  fprr->count--;
  return 0;
}

thread_t *fprr_get_next_thread(void *self) {
  sched_policy_fprr_t *fprr = self;
  if (fprr->count == 0) {
    return NULL;
  }

  thread_t *thread = LIST_FIRST(&fprr->queue);
  if (thread == NULL) {
    kassert(thread != NULL);
  }

  LIST_REMOVE(&fprr->queue, thread, list);
  fprr->count--;
  return thread;
}

//

sched_policy_impl_t sched_policy_fprr = {
  .init = fprr_init,
  .add_thread = fprr_add_thread,
  .remove_thread = fprr_remove_thread,
  .get_next_thread = fprr_get_next_thread
};
