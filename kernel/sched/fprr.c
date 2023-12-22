//
// Created by Aaron Gill-Braun on 2022-07-24.
//

#include <kernel/sched.h>

#include <kernel/string.h>
#include <kernel/panic.h>

#define ASSERT(x) kassert(x)
// #define ASSERT(x)
#define DPRINTF(fmt, ...)
// #define DPRINTF(x, ...) kprintf("sched_fprr: " x, ##__VA_ARGS__)


// Fixed Priority Round Robin Policy

typedef struct sched_policy_fprr {
  size_t count;
  LIST_HEAD(thread_t) queue;
  spinlock_t lock;
} sched_policy_fprr_t;

void *fprr_init(sched_t *sched) {
  sched_policy_fprr_t *fprr = kmallocz(sizeof(sched_policy_fprr_t));
  fprr->count = 0;
  LIST_INIT(&fprr->queue);
  spin_init(&fprr->lock);
  return fprr;
}

int fprr_add_thread(void *self, thread_t *td) {
  sched_policy_fprr_t *fprr = self;
  SPIN_LOCK(&fprr->lock);
  LIST_ADD(&fprr->queue, td, list);
  fprr->count++;
  SPIN_UNLOCK(&fprr->lock);
  return 0;
}

int fprr_remove_thread(void *self, thread_t *td) {
  sched_policy_fprr_t *fprr = self;
  SPIN_LOCK(&fprr->lock);
  LIST_REMOVE(&fprr->queue, td, list);
  fprr->count--;
  SPIN_UNLOCK(&fprr->lock);
  return 0;
}

thread_t *fprr_get_next_thread(void *self) {
  sched_policy_fprr_t *fprr = self;
  SPIN_LOCK(&fprr->lock);
  thread_t *td = LIST_FIRST(&fprr->queue);
  if (td != NULL) {
    LIST_REMOVE(&fprr->queue, td, list);
    fprr->count--;
  }
  SPIN_UNLOCK(&fprr->lock);
  return td;
}

//

sched_policy_impl_t sched_policy_fprr = {
  .init = fprr_init,
  .add_thread = fprr_add_thread,
  .remove_thread = fprr_remove_thread,
  .get_next_thread = fprr_get_next_thread
};

static void register_sched_policy_fprr() {
  if (sched_register_policy(SCHED_POLICY_FPRR, &sched_policy_fprr) < 0) {
    panic("failed to register fprr policy");
  }
}
STATIC_INIT(register_sched_policy_fprr);
