//
// Created by Aaron Gill-Braun on 2021-03-23.
//

#include <kernel/signal.h>
#include <kernel/proc.h>
#include <kernel/init.h>
#include <kernel/mm.h>
#include <kernel/tqueue.h>
#include <kernel/cpu/cpu.h>
#include <kernel/cpu/trapframe.h>

#include <kernel/mm/pool.h>
#include <kernel/printf.h>
#include <kernel/panic.h>

#include <kernel/mm/pgtable.h>

#define ASSERT(x) kassert(x)
#define LOG_TAG signal
#include <kernel/log.h>
#define EPRINTF(x, ...) kprintf("signal: %s: " x, __func__, ##__VA_ARGS__)

static pool_t *ksiginfo_pool;

static void ksiginfo_pool_init() {
  ksiginfo_pool = pool_create("ksiginfo", pool_sizes(sizeof(ksiginfo_t)), 0);
}
STATIC_INIT(ksiginfo_pool_init);

extern char sigtramp_trampoline[];

void static_init_setup_sigtramp(void *_) {
  ASSERT(is_aligned((uintptr_t) sigtramp_trampoline, PAGE_SIZE));

  int res;
  if ((res = vmap_protect((uintptr_t) sigtramp_trampoline, PAGE_SIZE, VM_RDEXC|VM_USER)) < 0) {
    panic("failed to protect sigtramp page {:err}", res);
  }
};
STATIC_INIT(static_init_setup_sigtramp);


// called from switch.asm and syscall.asm with a trapframe pointer.
// for user-mode signals: builds a sigframe on the user stack and modifies the
// trapframe to redirect userspace return to the trampoline. returns immediately
// so the kernel stack is fully unwound before the handler runs.
// for kernel-mode signals (SA_KERNHAND): calls the handler directly.
_used void signal_dispatch(struct trapframe *frame) {
  __assert_stack_is_aligned();
  thread_t *td = curthread;

  td_lock(td);

  struct siginfo info = {};
  while (sigqueue_pop(&td->sigqueue, &info, &td->sigmask) >= 0) {
    ASSERT(!sigset_masked(td->sigmask, info.si_signo));
    int sig = info.si_signo;

    struct sigaction act;
    if (sigacts_get(td->proc->sigacts, sig, &act) < 0) {
      continue;
    }

    if (act.sa_handler == SIG_IGN) {
      DPRINTF("signal %d ignored by thread {:td}\n", sig, td);
      continue;
    }

    if (act.sa_handler == SIG_DFL) {
      enum sigdisp disp = sig_to_dfl_disp(sig);
      if (disp == SIGDISP_IGN) {
        continue;
      } else if (disp == SIGDISP_TERM || disp == SIGDISP_CORE) {
        DPRINTF("signal %d default action: terminate thread {:td}\n", sig, td);
        td_unlock(td);
        proc_terminate(td->proc, 0, sig);
        return;
      }
      continue;
    }

    DPRINTF("dispatching signal %d for thread {:td}\n", sig, td);

    sigset_t saved_mask = td->sigmask;
    if (!(act.sa_flags & SA_NODEFER)) {
      sigset_mask(td->sigmask, sig);
    }
    sigset_block(&td->sigmask, &act.sa_mask);

    if (act.sa_flags & SA_KERNHAND) {
      td_unlock(td);
      act.sa_handler(sig);
      td_lock(td);
      td->sigmask = saved_mask;
      continue;
    }

    // user-mode signal: build sigframe on user stack, modify trapframe
    uintptr_t usp = (frame->rsp - sizeof(struct sigframe)) & ~0xFUL;
    struct sigframe *sf = (struct sigframe *) usp;

    if (vm_validate_ptr(usp, /*write=*/true) < 0 ||
        vm_validate_ptr(usp + sizeof(struct sigframe) - 1, /*write=*/true) < 0) {
      EPRINTF("sigframe at %p is not writable, killing thread {:td}\n", (void *)usp, td);
      td_unlock(td);
      proc_terminate(td->proc, 0, SIGSEGV);
      return;
    }

    memset(sf, 0, sizeof(struct sigframe));
    sf->info = info;
    sf->act = act;

    // save full register state into sigcontext
    struct sigcontext *ctx = &sf->ctx;
    ctx->r8 = frame->r8;
    ctx->r9 = frame->r9;
    ctx->r10 = frame->r10;
    ctx->r11 = frame->r11;
    ctx->r12 = frame->r12;
    ctx->r13 = frame->r13;
    ctx->r14 = frame->r14;
    ctx->r15 = frame->r15;
    ctx->rdi = frame->rdi;
    ctx->rsi = frame->rsi;
    ctx->rbp = frame->rbp;
    ctx->rbx = frame->rbx;
    ctx->rdx = frame->rdx;
    ctx->rax = frame->rax;
    ctx->rcx = frame->rcx;
    ctx->rsp = frame->rsp;
    ctx->rip = frame->rip;
    ctx->eflags = frame->rflags;
    ctx->oldmask = saved_mask.__bits[0];
    ctx->__reserved1[0] = saved_mask.__bits[1];

    // redirect trapframe to trampoline
    frame->rip = (uint64_t) sigtramp_trampoline;
    frame->rsp = usp;
    frame->rdi = sig;
    frame->rsi = (uint64_t) &sf->info;
    frame->rdx = (uint64_t) &sf->ctx;
    frame->rflags = 0x202; // IF set
    frame->flags |= TF_SYSRET;

    // only deliver one user-mode signal per call
    if (td->sigqueue.count == 0) {
      td->flags2 &= ~TDF2_SIGPEND;
    }
    td_unlock(td);
    return;
  }

  // no signals dispatched (all ignored/masked)
  if (td->sigqueue.count == 0) {
    td->flags2 &= ~TDF2_SIGPEND;
  }

  td_unlock(td);
}

// called from handle_syscall when the user invokes rt_sigreturn.
// restores the original register context from the sigframe on the user stack.
_used void sys_rt_sigreturn_impl(struct trapframe *frame) {
  // the user's rsp at syscall entry points to the sigframe (trampoline restored
  // rsp to sigframe pointer before the syscall instruction)
  struct sigframe *sf = (struct sigframe *) frame->rsp;

  if (vm_validate_ptr((uintptr_t) sf, /*write=*/false) < 0 ||
      vm_validate_ptr((uintptr_t) sf + sizeof(struct sigframe) - 1, /*write=*/false) < 0) {
    EPRINTF("invalid sigframe at %p\n", sf);
    proc_terminate(curproc, 0, SIGSEGV);
    return;
  }

  struct sigcontext *ctx = &sf->ctx;

  // restore all registers into trapframe
  frame->r8 = ctx->r8;
  frame->r9 = ctx->r9;
  frame->r10 = ctx->r10;
  frame->r11 = ctx->r11;
  frame->r12 = ctx->r12;
  frame->r13 = ctx->r13;
  frame->r14 = ctx->r14;
  frame->r15 = ctx->r15;
  frame->rdi = ctx->rdi;
  frame->rsi = ctx->rsi;
  frame->rbp = ctx->rbp;
  frame->rbx = ctx->rbx;
  frame->rdx = ctx->rdx;
  frame->rax = ctx->rax;
  frame->rcx = ctx->rcx;
  frame->rsp = ctx->rsp;
  frame->rip = ctx->rip;
  frame->rflags = ctx->eflags;

  // restore signal mask
  thread_t *td = curthread;
  td_lock(td);
  td->sigmask.__bits[0] = ctx->oldmask;
  td->sigmask.__bits[1] = ctx->__reserved1[0];
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

int signal_deliver_self_sync(siginfo_t *info, struct trapframe *frame) {
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
    pr_unlock(proc);
    proc_stop(proc, sig);
    return 0;
  } else if (disp == SIGDISP_CONT) {
    proc_cont(proc);
    if (act.sa_handler == SIG_DFL || act.sa_handler == SIG_IGN) {
      return 0;
    }
  }

  // set up user signal handler via trapframe, same as signal_dispatch
  sigset_t saved_mask = td->sigmask;
  if (!(act.sa_flags & SA_NODEFER)) {
    sigset_mask(td->sigmask, sig);
  }
  sigset_block(&td->sigmask, &act.sa_mask);

  uintptr_t usp = (frame->rsp - sizeof(struct sigframe)) & ~0xFUL;
  struct sigframe *sf = (struct sigframe *) usp;

  if (vm_validate_ptr(usp, /*write=*/true) < 0 ||
      vm_validate_ptr(usp + sizeof(struct sigframe) - 1, /*write=*/true) < 0) {
    EPRINTF("sigframe at %p is not writable, killing thread {:td}\n", (void *)usp, td);
    td_unlock(td);
    proc_terminate(td->proc, 0, SIGSEGV);
    return 0;
  }

  memset(sf, 0, sizeof(struct sigframe));
  sf->info = *info;
  sf->act = act;

  struct sigcontext *ctx = &sf->ctx;
  ctx->r8 = frame->r8;
  ctx->r9 = frame->r9;
  ctx->r10 = frame->r10;
  ctx->r11 = frame->r11;
  ctx->r12 = frame->r12;
  ctx->r13 = frame->r13;
  ctx->r14 = frame->r14;
  ctx->r15 = frame->r15;
  ctx->rdi = frame->rdi;
  ctx->rsi = frame->rsi;
  ctx->rbp = frame->rbp;
  ctx->rbx = frame->rbx;
  ctx->rdx = frame->rdx;
  ctx->rax = frame->rax;
  ctx->rcx = frame->rcx;
  ctx->rsp = frame->rsp;
  ctx->rip = frame->rip;
  ctx->eflags = frame->rflags;
  ctx->oldmask = saved_mask.__bits[0];
  ctx->__reserved1[0] = saved_mask.__bits[1];

  frame->rip = (uint64_t) sigtramp_trampoline;
  frame->rsp = usp;
  frame->rdi = sig;
  frame->rsi = (uint64_t) &sf->info;
  frame->rdx = (uint64_t) &sf->ctx;
  frame->rflags = 0x202;
  frame->flags |= TF_SYSRET;

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
    pool_free(ksiginfo_pool, ksig);
  }
  queue->count = 0;
}

void sigqueue_push(sigqueue_t *queue, struct siginfo *info) {
  ASSERT(queue->count < INT32_MAX);
  ksiginfo_t *ksig = pool_alloc(ksiginfo_pool, sizeof(ksiginfo_t));
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
  pool_free(ksiginfo_pool, ksig);
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

DEFINE_SYSCALL(rt_sigtimedwait, int, const sigset_t *uthese, struct siginfo *uinfo, const struct timespec *uts, size_t sigsetsize) {
  DPRINTF("syscall: rt_sigtimedwait\n");
  if (uthese == NULL) {
    return -EINVAL;
  }
  if (vm_validate_ptr((uintptr_t) uthese, /*write=*/false) < 0) {
    return -EFAULT;
  }
  if (uinfo != NULL && vm_validate_ptr((uintptr_t) uinfo, /*write=*/true) < 0) {
    return -EFAULT;
  }
  if (uts != NULL && vm_validate_ptr((uintptr_t) uts, /*write=*/false) < 0) {
    return -EFAULT;
  }

  sigset_t these = *uthese;
  proc_t *proc = curproc;
  thread_t *td = curthread;

  // try to dequeue a matching signal
  siginfo_t info;
  while (true) {
    // scan the queue for a signal in the requested set
    SLIST_FOR_IN(ksig, LIST_FIRST(&td->sigqueue.list), next) {
      if (sigset_masked(these, ksig->info.si_signo)) {
        info = ksig->info;
        SLIST_REMOVE(&td->sigqueue.list, ksig, next);
        td->sigqueue.count--;
        pool_free(ksiginfo_pool, ksig);
        if (uinfo != NULL) {
          *uinfo = info;
        }
        return info.si_signo;
      }
    }

    // no matching signal pending, wait for one
    pr_lock(proc);
    int res;
    if (uts != NULL) {
      struct timespec ts = *uts;
      res = cond_wait_sigtimeout(&proc->signal_cond, &proc->lock, &ts);
    } else {
      res = cond_wait_sig(&proc->signal_cond, &proc->lock);
    }
    pr_unlock(proc);

    if (res == -ETIMEDOUT) {
      return -EAGAIN;
    }
  }
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

DEFINE_SYSCALL(sigaltstack, int, const stack_t *ss, stack_t *oss) {
  if (ss != NULL && vm_validate_ptr((uintptr_t) ss, /*write=*/false) < 0) {
    return -EFAULT;
  }
  if (oss != NULL && vm_validate_ptr((uintptr_t) oss, /*write=*/true) < 0) {
    return -EFAULT;
  }

  thread_t *td = curthread;

  if (oss != NULL) {
    *oss = td->sigstack;
    if (oss->ss_sp == NULL) {
      oss->ss_flags = SS_DISABLE;
    }
  }

  if (ss != NULL) {
    if (td->sigstack.ss_flags & SS_ONSTACK) {
      return -EPERM;
    }

    int flags = ss->ss_flags & ~SS_ONSTACK;
    if (flags & ~SS_DISABLE) {
      return -EINVAL;
    }

    if (flags & SS_DISABLE) {
      td->sigstack.ss_sp = NULL;
      td->sigstack.ss_flags = SS_DISABLE;
      td->sigstack.ss_size = 0;
    } else {
      if (ss->ss_size < MINSIGSTKSZ) {
        return -ENOMEM;
      }
      td->sigstack.ss_sp = ss->ss_sp;
      td->sigstack.ss_flags = 0;
      td->sigstack.ss_size = ss->ss_size;
    }
  }

  return 0;
}

DEFINE_SYSCALL(rt_sigsuspend, int, const sigset_t *set, size_t sigsetsize) {
  todo("rt_sigsuspend");
}
