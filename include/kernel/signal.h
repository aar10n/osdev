//
// Created by Aaron Gill-Braun on 2021-03-23.
//

#ifndef KERNEL_SIGNAL_H
#define KERNEL_SIGNAL_H

#include <kernel/base.h>
#include <kernel/queue.h>
#include <kernel/mutex.h>
#include <kernel/sigframe.h>

#include <abi/signal.h>

typedef struct proc proc_t;

enum sigdisp {
  SIGDISP_IGN,
  SIGDISP_TERM,
  SIGDISP_CORE,
  SIGDISP_STOP,
  SIGDISP_CONT,
  SIGDISP_HANDLER,
};

#define sigset_masked(set, sig) ((set).__bits[(sig) / 64] & (1ULL << ((sig) % 64)))
#define sigset_mask(set, sig) ((set).__bits[(sig) / 64] |= (1ULL << ((sig) % 64)))
#define sigset_unmask(set, sig) ((set).__bits[(sig) / 64] &= ~(1ULL << ((sig) % 64)))
static inline void sigset_block(sigset_t *set, const sigset_t *mask) {
  for (int i = 0; i < sizeof(sigset_t) / sizeof(uint64_t); i++) {
    set->__bits[i] |= mask->__bits[i];
  }
}
static inline void sigset_unblock(sigset_t *set, const sigset_t *mask) {
  for (int i = 0; i < sizeof(sigset_t) / sizeof(uint64_t); i++) {
    set->__bits[i] &= ~mask->__bits[i];
  }
}

struct sigacts {
  struct sigaction std_actions[SIGRTMIN]; // standard signal actions
  struct sigaction *rt_actions;           // realtime signal actions
  mtx_t lock;                             // sigacts lock
};

struct sigacts *sigacts_alloc();
struct sigacts *sigacts_clone(struct sigacts *sa);
void sigacts_free(struct sigacts **sap);
void sigacts_reset(struct sigacts *sa);
int sigacts_get(struct sigacts *sa, int sig, struct sigaction *act, enum sigdisp *disp);
int sigacts_set(struct sigacts *sa, int sig, const struct sigaction *act, struct sigaction *oact);

typedef struct ksiginfo {
  struct siginfo info;
  int flags;
  SLIST_ENTRY(struct ksiginfo) next;
} ksiginfo_t;

typedef struct sigqueue {
  LIST_HEAD(struct ksiginfo) list;  // list of pending signals
} sigqueue_t;

void sigqueue_init(sigqueue_t *queue);
void sigqueue_push(sigqueue_t *queue, struct siginfo *info);
int sigqueue_pop(sigqueue_t *queue, struct siginfo *info, const sigset_t *mask);
int sigqueue_getpending(sigqueue_t *queue, sigset_t *set, const sigset_t *mask);

#endif
