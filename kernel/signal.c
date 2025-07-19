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
    if ((res = sigacts_get(td->proc->sigacts, sig, &act)) < 0) {
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
    sigtramp_entry(&info, &act, user_mode); // execute the signal handler
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

//

const char *sig_name(int sig) {
  ASSERT(sig > 0 && sig < NSIG);
  switch (sig) {
    case SIGHUP: return "SIGHUP";
    case SIGINT: return "SIGINT";
    case SIGQUIT: return "SIGQUIT";
    case SIGILL: return "SIGILL";
    case SIGTRAP: return "SIGTRAP";
    case SIGABRT: return "SIGABRT";
    case SIGFPE: return "SIGFPE";
    case SIGKILL: return "SIGKILL";
    case SIGBUS: return "SIGBUS";
    case SIGSEGV: return "SIGSEGV";
    case SIGPIPE: return "SIGPIPE";
    case SIGALRM: return "SIGALRM";
    case SIGTERM: return "SIGTERM";
    case SIGUSR1: return "SIGUSR1";
    case SIGUSR2: return "SIGUSR2";
    case SIGCHLD: return "SIGCHLD";
    case SIGCONT: return "SIGCONT";
    case SIGSTOP: return "SIGSTOP";
    case SIGTSTP: return "SIGTSTP";
    case SIGTTIN: return "SIGTTIN";
    case SIGTTOU: return "SIGTTOU";
    default: return "UNKNOWN";
  }
}

enum sigdisp sig_to_dfl_disp(int sig) {
  ASSERT(sig > 0 && sig < NSIG);
  switch (sig) {
    case SIGHUP: case SIGINT: case SIGQUIT: case SIGKILL:
    case SIGUSR1: case SIGUSR2: case SIGPIPE: case SIGALRM:
    case SIGTERM: case SIGVTALRM: case SIGPROF: case SIGPOLL:
    case SIGPWR: case SIGRTMIN ... SIGRTMAX:
      return SIGDISP_TERM;
    case SIGILL: case SIGTRAP: case SIGABRT: case SIGBUS:
    case SIGFPE: case SIGSEGV: case SIGXCPU: case SIGXFSZ:
    case SIGSYS:
      return SIGDISP_CORE;
    case SIGSTOP: case SIGTSTP: case SIGTTIN: case SIGTTOU:
      return SIGDISP_STOP;
    case SIGCONT:
      return SIGDISP_CONT;
    case SIGCHLD: case SIGURG: case SIGWINCH:
      return SIGDISP_IGN;
    default:
      unreachable;
  }
}

enum sigdisp sigaction_to_disp(int sig, const struct sigaction *sa) {
  if (sig == SIGKILL) {
    return SIGDISP_TERM;
  } else if (sig == SIGSTOP) {
    return SIGDISP_STOP;
  } else if (sig == SIGCONT) {
    return SIGDISP_CONT;
  } else if (sa->sa_handler == SIG_IGN) {
    return SIGDISP_IGN;
  } else if (sa->sa_handler == SIG_DFL) {
    return sig_to_dfl_disp(sig);
  }
  return SIGDISP_USER;
}

//

int signal_deliver_self_sync(siginfo_t *info) {
  thread_t *td = curthread;
  proc_t *proc = td->proc;
  td_lock_assert(td, LA_OWNED);

  int res;
  struct sigaction act;
  int sig = info->si_signo;
  if ((res = sigacts_get(proc->sigacts, sig, &act)) < 0) {
    EPRINTF("failed to get sigaction for signal %d: {:err}\n", sig, res);
    return res;
  }

  enum sigdisp disp = sigaction_to_disp(sig, &act);
  if (disp == SIGDISP_IGN) {
    // signal was explicitly ignored, or the default action is to ignore it
    EPRINTF("signal %s ignored by thread {:td}\n", sig_name(sig), td);
    return 0;
  } else if (disp == SIGDISP_TERM) {
    td_unlock(td);
    proc_terminate(proc, 0, sig);
    return 0;
  } else if (disp == SIGDISP_CORE) {
    td_unlock(td);
    proc_coredump(proc, info);
    return 0;
  } else if (disp == SIGDISP_STOP) {
    proc_stop(proc, sig);
    return 0;
  } else if (disp == SIGDISP_CONT) {
    proc_cont(proc);
    if (act.sa_handler == SIG_DFL || act.sa_handler == SIG_IGN) {
      // no user handler
      return 0;
    }
  }

  todo("synchronous user handled signals not supported");
  return 1;
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

int sigacts_get(struct sigacts *sa, int sig, struct sigaction *act) {
  if (sig <= 0 || sig >= NSIG) {
    return -EINVAL;
  } else if (act == NULL) {
    return 0;
  }

  mtx_lock(&sa->lock);
  struct sigaction sigact = {0};
  if (sig < SIGRTMIN) {
    // standard signal
    sigact = sa->std_actions[sig];
  } else if (sa->rt_actions != NULL) {
    // realtime signal
    sigact = sa->rt_actions[sig];
  } else {
    // no action set, use default
    sigact.sa_handler = SIG_DFL;
    sigact.sa_flags = 0;
  }

  if (act != NULL)
    memcpy(act, &sigact, sizeof(struct sigaction));

  mtx_unlock(&sa->lock);
  return 0;
}

int sigacts_set(struct sigacts *sa, int sig, const struct sigaction *act, struct sigaction *oact) {
  if (sig <= 0 || sig >= NSIG || sig == SIGKILL || sig == SIGSTOP) {
    return -EINVAL;
  }

  int index;
  struct sigaction *array;
  mtx_lock(&sa->lock);
  if (sig < SIGRTMIN) {
    array = sa->std_actions;
    index = sig;
  } else if (sa->rt_actions != NULL) {
    array = sa->rt_actions;
    index = sig - SIGRTMIN;
  } else {
    unreachable;
  }

  if (oact != NULL) {
    // copy the old action if requested
    memcpy(oact, &array[index], sizeof(struct sigaction));
  }
  if (act != NULL) {
    memcpy(&array[index], act, sizeof(struct sigaction));
  } else {
    // if act is NULL, we reset the action to default
    array[index] = (struct sigaction) {
      .sa_handler = SIG_DFL,
      .sa_flags = 0,
      .sa_mask = {0},
    };
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
