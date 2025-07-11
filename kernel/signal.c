//
// Created by Aaron Gill-Braun on 2021-03-23.
//

#include <kernel/signal.h>
#include <kernel/proc.h>
#include <kernel/init.h>
#include <kernel/mm.h>
#include <kernel/cpu/cpu.h>

#include <kernel/printf.h>
#include <kernel/panic.h>

#include <kernel/mm/pgtable.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(x, ...) kprintf("signal: " x, ##__VA_ARGS__)
#define EPRINTF(x, ...) kprintf("signal: %s: " x, __func__, ##__VA_ARGS__)

extern void sigtramp_entry(struct siginfo *info, const struct sigaction *act, bool user_mode);

void static_init_setup_sigtramp(void *_) {
  ASSERT(is_aligned((uintptr_t) sigtramp_entry, PAGE_SIZE));

  int res;
  if ((res = vmap_protect((uintptr_t) sigtramp_entry, PAGE_SIZE, VM_RDEXC|VM_USER)) < 0) {
    panic("failed to protect sigtramp page {:err}", res);
  }
};
STATIC_INIT(static_init_setup_sigtramp);


// called from switch.asm
_used void signal_dispatch() {
  __assert_stack_is_aligned();
  // this function executes all pending signals for the current thread
  thread_t *td = curthread;

  // hold the thread lock while we process the signal queue
  td_lock(td);

  int remaining;
  struct siginfo info = {};
  while ((remaining = sigqueue_pop(&td->sigqueue, &info, &td->sigmask)) >= 0) {
    // sigqueue_pop should only ever return an unmasked signal
    ASSERT(!sigset_masked(td->sigmask, info.si_signo));
    int sig = info.si_signo;
    int res;

    // get the signal action for this signal
    struct sigaction act;
    if ((res = sigacts_get(td->proc->sigacts, sig, &act, NULL)) < 0) {
      continue;
    }

    if (act.sa_handler == SIG_IGN) {
      // signal is ignored
      DPRINTF("signal %d ignored by thread {:td}\n", sig, td);
      continue;
    }

    DPRINTF("dispatching signal %d for thread {:td}\n", sig, td);

    uintptr_t kstack_ptr = td->kstack_ptr; // save kstack pointer
    bool user_mode = !(act.sa_flags & SA_KERNHAND);
    td_unlock(td); // unlock the thread while the signal is handled
    // execute the signal handler
    sigtramp_entry(&info, &act, user_mode);
    td_lock(td); // relock it in case there are more signals to handle
    td->kstack_ptr = kstack_ptr; // restore kstack pointer

    DPRINTF("signal %d handled by thread {:td}\n", sig, td);
  }

  if (td->sigqueue.count == 0) {
    // we can clear the thread TDF2_SIGPEND flag
    td->flags2 &= ~TDF2_SIGPEND;
  }

  // finally unlock the thread
  td_unlock(td);
}

static void term_handler(int sig, struct siginfo *info, void *arg) {
  struct sigframe *sf = arg;
  todo();
}

static void core_handler(int sig, struct siginfo *info, void *arg) {
  struct sigframe *sf = arg;
  todo("core_handler");
}

static void stop_handler(int sig, struct siginfo *info, void *arg) {
  struct sigframe *sf = arg;
  todo("stop_handler");
}

static void cont_handler(int sig, struct siginfo *info, void *arg) {
  struct sigframe *sf = arg;
  todo("cont_handler");
}

//

static struct sigaction ign_action = {
  .sa_handler = SIG_IGN,
  .sa_flags = 0,
  .sa_mask = {0},
};

static struct sigaction term_action = {
  .sa_sigaction = term_handler,
  .sa_flags = SA_SIGINFO|SA_KERNHAND,
  .sa_mask = {0},
};

static struct sigaction core_action = {
  .sa_sigaction = core_handler,
  .sa_flags = SA_SIGINFO|SA_KERNHAND,
  .sa_mask = {0},
};

static struct sigaction stop_action = {
  .sa_sigaction = stop_handler,
  .sa_flags = SA_SIGINFO|SA_KERNHAND,
  .sa_mask = {0},
};

static struct sigaction cont_action = {
  .sa_sigaction = cont_handler,
  .sa_flags = SA_SIGINFO|SA_KERNHAND,
  .sa_mask = {0},
};

static inline struct sigaction *signal_get_default_action(int sig) {
  if (sig <= 0 || sig >= NSIG) {
    return NULL;
  }
  switch (sig) {
    case SIGHUP:
    case SIGINT:
    case SIGQUIT:
    case SIGKILL:
    case SIGUSR1:
    case SIGUSR2:
    case SIGPIPE:
    case SIGALRM:
    case SIGTERM:
    case SIGVTALRM:
    case SIGPROF:
    case SIGPOLL:
    case SIGPWR:
      return &term_action;
    case SIGILL:
    case SIGTRAP:
    case SIGABRT:
    case SIGBUS:
    case SIGFPE:
    case SIGSEGV:
    case SIGXCPU:
    case SIGXFSZ:
    case SIGSYS:
      return &core_action;
    case SIGSTOP:
    case SIGTSTP:
    case SIGTTIN:
    case SIGTTOU:
      return &stop_action;
    case SIGCONT:
      return &cont_action;
    default:
      return &ign_action;
  }
}

static inline enum sigdisp signal_action_to_disp(const struct sigaction *sa) {
  ASSERT(sa->sa_handler != SIG_DFL);

  if (sa->sa_handler == SIG_IGN) {
    return SIGDISP_IGN;
  } else if (sa->sa_sigaction == term_handler) {
    return SIGDISP_TERM;
  } else if (sa->sa_sigaction == core_handler) {
    return SIGDISP_CORE;
  } else if (sa->sa_sigaction == stop_handler) {
    return SIGDISP_STOP;
  } else if (sa->sa_sigaction == cont_handler) {
    return SIGDISP_CONT;
  } else {
    return SIGDISP_HANDLER;
  }
}


//
// MARK: sigacts
//

struct sigacts *sigacts_alloc() {
  struct sigacts *sa = kmallocz(sizeof(struct sigacts));
  mtx_init(&sa->lock, 0, "sigacts_lock");

  // init standard signals
  for (int i = 0; i < SIGRTMIN; i++) {
    sa->std_actions[i] = (struct sigaction) {
      .sa_handler = SIG_DFL,
      .sa_flags = 0,
      .sa_mask = {0},
    };
  }
  return sa;
}

struct sigacts *sigacts_clone(struct sigacts *sa) {
  struct sigacts *clone = kmallocz(sizeof(struct sigacts));
  mtx_init(&clone->lock, 0, "sigacts_lock");

  mtx_lock(&sa->lock);
  memcpy(clone->std_actions, sa->std_actions, sizeof(clone->std_actions));
  if (sa->rt_actions != NULL) {
    clone->rt_actions = kmalloc(sizeof(struct sigaction) * NRRTSIG);
    memcpy(clone->rt_actions, sa->rt_actions, sizeof(struct sigaction) * NRRTSIG);
  }
  mtx_unlock(&sa->lock);
  return clone;
}

void sigacts_free(struct sigacts **sap) {
  struct sigacts *sa = *moveptr(sap);
  mtx_assert(&sa->lock, MA_NOTOWNED);
  mtx_destroy(&sa->lock);
  if (sa->rt_actions != NULL) {
    kfree(sa->rt_actions);
  }
  kfree(sa);
}

void sigacts_reset(struct sigacts *sa) {
  mtx_lock(&sa->lock);
  // reset standard signals to default
  for (int i = 0; i < SIGRTMIN; i++) {
    sa->std_actions[i] = (struct sigaction) {
      .sa_handler = SIG_DFL,
      .sa_flags = 0,
      .sa_mask = {0},
    };
  }
  // free realtime actions if they exist
  if (sa->rt_actions != NULL) {
    kfree(sa->rt_actions);
    sa->rt_actions = NULL;
  }
  mtx_unlock(&sa->lock);
}

int sigacts_get(struct sigacts *sa, int sig, struct sigaction *act, enum sigdisp *disp) {
  if (sig <= 0 || sig >= NSIG) {
    return -EINVAL;
  } else if (act == NULL && disp == NULL) {
    return 0;
  }

  mtx_lock(&sa->lock);
  struct sigaction *sigact;
  enum sigdisp sigdisp = SIGDISP_IGN;
  if (sig < SIGRTMIN) {
    // standard signal
    sigact = &sa->std_actions[sig];
  } else if (sa->rt_actions != NULL) {
    // realtime signal
    sigact = &sa->rt_actions[sig];
  } else {
    // no action set, use default
    sigact = signal_get_default_action(sig);
  }

  if (sigact->sa_handler == SIG_DFL) {
    // replace with actual default action
    sigact = signal_get_default_action(sig);
  }

  if (act != NULL)
    memcpy(act, sigact, sizeof(struct sigaction));
  if (disp != NULL)
    *disp = signal_action_to_disp(sigact);

  mtx_unlock(&sa->lock);
  return 0;
}

int sigacts_set(struct sigacts *sa, int sig, const struct sigaction *act, struct sigaction *oact) {
  if (sig <= 0 || sig >= NSIG || sig == SIGKILL || sig == SIGSTOP) {
    return -EINVAL;
  }

  mtx_lock(&sa->lock);
  if (sig < SIGRTMIN) {
    // standard signal
    if (oact != NULL) {
      memcpy(oact, &sa->std_actions[sig], sizeof(struct sigaction));
    }
    memcpy(&sa->std_actions[sig], act, sizeof(struct sigaction));
  } else {
    // realtime signal
    if (sa->rt_actions == NULL) {
      sa->rt_actions = kmalloc(sizeof(struct sigaction) * NRRTSIG);
      memset(sa->rt_actions, 0, sizeof(struct sigaction) * NRRTSIG);
    }

    if (oact != NULL) {
      memcpy(oact, &sa->rt_actions[sig - SIGRTMIN], sizeof(struct sigaction));
    }
    memcpy(&sa->rt_actions[sig - SIGRTMIN], act, sizeof(struct sigaction));
  }
  mtx_unlock(&sa->lock);
  return 0;
}

// MARK: sigqueue

void sigqueue_init(sigqueue_t *queue) {
  queue->count = 0;
  LIST_INIT(&queue->list);
}

void sigqueue_clear(sigqueue_t *queue) {
  ksiginfo_t *ksig;
  while ((ksig = LIST_FIRST(&queue->list)) != NULL) {
    SLIST_REMOVE(&queue->list, ksig, next);
    kfree(ksig);
  }
  queue->count = 0;
}

void sigqueue_push(sigqueue_t *queue, struct siginfo *info) {
  ASSERT(queue->count < INT32_MAX);
  ksiginfo_t *ksig = kmallocz(sizeof(ksiginfo_t));
  ASSERT(ksig != NULL);
  ksig->info = *info;
  queue->count++;
  SLIST_ADD(&queue->list, ksig, next);
}

int sigqueue_pop(sigqueue_t *queue, struct siginfo *info, const sigset_t *mask) {
  if (LIST_EMPTY(&queue->list)) {
    return -EAGAIN;
  }

  int count = 0;
  ksiginfo_t *ksig = SLIST_FIND(sig, LIST_FIRST(&queue->list), next, !sigset_masked(*mask, sig->info.si_signo));
  if (ksig == NULL) {
    return -EAGAIN; // no unmasked signals
  }
  SLIST_REMOVE(&queue->list, ksig, next);
  queue->count--;

  *info = ksig->info;
  kfree(ksig);
  return 0;
}

int sigqueue_getpending(sigqueue_t *queue, sigset_t *set, const sigset_t *mask) {
  memset(set, 0, sizeof(sigset_t));
  if (LIST_EMPTY(&queue->list)) {
    return -EAGAIN;
  }

  SLIST_FOR_IN(ksig, LIST_FIRST(&queue->list), next) {
    if (sigset_masked(*mask, ksig->info.si_signo)) {
      sigset_mask(*set, ksig->info.si_signo);
    }
  }
  return 0;
}

//
// MARK: System Calls
//

DEFINE_SYSCALL(rt_sigaction, int, int sig, const struct sigaction *act, struct sigaction *oact) {
  if (act != NULL && vm_validate_ptr((uintptr_t) act, /*write=*/false) < 0) {
    return -EFAULT;
  }
  if (oact != NULL && vm_validate_ptr((uintptr_t) oact, /*write=*/true) < 0) {
    return -EFAULT;
  }
  return sigacts_set(curproc->sigacts, sig, act, oact);
}

DEFINE_SYSCALL(rt_sigprocmask, int, int how, const sigset_t *set, sigset_t *oset, size_t sigsetsize) {
  if (how < 0 || how > 3) {
    return -EINVAL;
  } else if (set == NULL && oset == NULL) {
    return 0;
  }

  if (set != NULL && vm_validate_ptr((uintptr_t) set, /*write=*/false) < 0) {
    return -EFAULT;
  }
  if (oset != NULL && vm_validate_ptr((uintptr_t) oset, /*write=*/true) < 0) {
    return -EFAULT;
  }

  if (oset != NULL) {
    memcpy(oset, &curthread->sigmask, sigsetsize);
  }

  if (set != NULL) {
    sigset_t real_set = {0};
    memcpy(&real_set, set, sigsetsize);
    sigset_unmask(real_set, SIGKILL);
    sigset_unmask(real_set, SIGSTOP);
    switch (how) {
      case SIG_BLOCK:
        sigset_block(&curthread->sigmask, &real_set);
        break;
      case SIG_UNBLOCK:
        sigset_unblock(&curthread->sigmask, &real_set);
        break;
      case SIG_SETMASK:
        curthread->sigmask = real_set;
        break;
      default:
        EPRINTF("rt_sigprocmask: invalid how {:d}\n", how);
        return -EINVAL;
    }
  }
  return 0;
}

DEFINE_SYSCALL(rt_sigpending, int, sigset_t *set, size_t sigsetsize) {
  if (set == NULL) {
    return -EINVAL;
  }

  if (vm_validate_ptr((uintptr_t) set, /*write=*/true) < 0) {
    return -EFAULT;
  }

  sigset_t real_set = {0};
  if (sigqueue_getpending(&curthread->sigqueue, &real_set, &curthread->sigmask) < 0) {
    // no pending signals
    memset(set, 0, sigsetsize);
  } else {
    memcpy(set, &real_set, sigsetsize);
  }
  return 0;
}

DEFINE_SYSCALL(rt_sigsuspend, int, const sigset_t *set, size_t sigsetsize) {
  todo("rt_sigsuspend");
}
