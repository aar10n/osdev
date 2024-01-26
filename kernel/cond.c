//
// Created by Aaron Gill-Braun on 2023-12-28.
//

#include <kernel/cond.h>


void cond_init(cond_t *cond, const char *desc) {
  // todo();
}

void cond_destroy(cond_t *cond) {
  todo();
}

void cond_wait(cond_t *cond, struct lock_object *lock) {
  todo();
}

int cond_wait_sig(cond_t *cond, struct lock_object *lock) {
  todo();
}

// int cond_timedwait(cond_t *cond, struct lock_object *lock, uint64_t timeout) {
//   todo();
// }
// int cond_timedwait_sig(cond_t *cond, )

void cond_signal(cond_t *cond) {
  todo();
}

void cond_broadcast(cond_t *cond) {
  todo();
}
