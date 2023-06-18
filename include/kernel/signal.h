//
// Created by Aaron Gill-Braun on 2021-03-23.
//

#ifndef KERNEL_SIGNAL_H
#define KERNEL_SIGNAL_H

#include <kernel/base.h>
#include <kernel/mutex.h>
#include <abi/signal.h>
#include <kernel/process.h>

#define sig_masked(thread, sig) ((thread)->signal & (1 << (sig)))

#define SIG_DEFAULT 0
#define SIG_IGNORE  1
#define SIG_ACTION  2

typedef struct signal {
  pid_t source;
  pid_t dest;
  int signo;
  uintptr_t value;
} signal_t;

typedef struct sig_handler {
  int type;
  int flags;
  sigset_t mask;
  cond_t signal;
  union {
    void (*handler)(int);
    void (*sigaction)(int, siginfo_t *, void *);
  };
} sig_handler_t;


void signal_init_handlers(process_t *process);
int signal_send(pid_t pid, int sig, sigval_t value);
int signal_getaction(pid_t pid, int sig, sigaction_t *oact);
int signal_setaction(pid_t pid, int sig, int type, const sigaction_t *act);

#endif
