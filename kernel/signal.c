//
// Created by Aaron Gill-Braun on 2021-03-23.
//

#include <kernel/signal.h>
#include <kernel/process.h>
#include <kernel/thread.h>
#include <kernel/panic.h>
#include <kernel/mm.h>
#include <kernel/string.h>

extern void thread_sighandle(uintptr_t fn, uintptr_t rsp);

//

static int abort_handler(thread_t *thread) {
  // not implemented
  panic("[abort] process: %d\n", thread->process->pid);
}

static int terminate_handler(thread_t *thread) {
  // not implemented
  panic("[terminate] process: %d\n", thread->process->pid);
}

static int coredump_handler(thread_t *thread) {
  // not implemented
  panic("[core dump] process: %d\n", thread->process->pid);
}

static int ignore_handler(thread_t *thread) {
  return 0;
}

static int continue_hanlder(thread_t *thread) {
  // not implemented
  panic("[continue] process: %d\n", thread->process->pid);
}

static int stop_handler(thread_t *thread) {
  // not implemented
  panic("[stop] process: %d\n", thread->process->pid);
}


int (*default_handlers[])(thread_t *thread) = {
  [SIGHUP] = terminate_handler,
  [SIGINT] = terminate_handler,
  [SIGQUIT] = abort_handler,
  [SIGILL] = coredump_handler,
  [SIGTRAP] = abort_handler,
  [SIGABRT] = abort_handler,
  [SIGBUS] = abort_handler,
  [SIGFPE] = abort_handler,
  [SIGKILL] = terminate_handler,
  [SIGUSR1] = terminate_handler,
  [SIGSEGV] = abort_handler,
  [SIGUSR2] = terminate_handler,
  [SIGPIPE] = terminate_handler,
  [SIGALRM] = terminate_handler,
  [SIGTERM] = terminate_handler,
  [SIGSTKFLT] = terminate_handler,
  [SIGCHLD] = ignore_handler,
  [SIGCONT] = continue_hanlder,
  [SIGSTOP] = stop_handler,
  [SIGTSTP] = stop_handler,
  [SIGTTIN] = stop_handler,
  [SIGTTOU] = stop_handler,
  [SIGURG] = ignore_handler,
  [SIGXCPU] = abort_handler,
  [SIGXFSZ] = abort_handler,
  [SIGVTALRM] = terminate_handler,
  [SIGPROF] = terminate_handler,
  [SIGWINCH] = ignore_handler,
  [SIGPOLL] = terminate_handler,
  [SIGPWR] = terminate_handler,
  [SIGSYS] = abort_handler,
};

//

// void signal_init_handlers(process_t *process) {
//   process->sig_handlers[0]= NULL;
//   for (int i = 1; i < NSIG; i++) {
//     sig_handler_t *handler = kmalloc(sizeof(sig_handler_t));
//     handler->type = SIG_DEFAULT;
//     handler->flags = 0;
//     handler->mask = 0;
//     handler->handler = NULL;
//     cond_init(&handler->signal, 0);
//     process->sig_handlers[i] = handler;
//   }
// }
//
// int signal_send(pid_t pid, int sig, sigval_t value) {
//   kassert(pid > 0);
//   if (pid >= MAX_PROCS) {
//     ERRNO = ESRCH;
//     return -1;
//   } else if (sig < 0 || sig >= NSIG) {
//     ERRNO = EINVAL;
//     return -1;
//   }
//
//   process_t *process = process_get(pid);
//   if (process == NULL) {
//     ERRNO = ESRCH;
//     return -1;
//   } else if (sig == 0) {
//     // null signal return success
//     return 0;
//   }
//
//   sig_handler_t *handler = process->sig_handlers[sig];
//   if (handler->type == SIG_IGNORE) {
//     return 0;
//   }
//
//   thread_t *thread = process_get_sigthread(process, sig);
//   mutex_lock(&process->sig_mutex);
//   if (handler->type == SIG_DEFAULT) {
//     return default_handlers[sig](thread);
//   }
//
//   // save the current context
//   thread_meta_ctx_t *mctx = thread->mctx;
//   memcpy(&mctx->ctx, thread->ctx, sizeof(thread_ctx_t));
//   mctx->kernel_sp = thread->kernel_sp;
//   mctx->user_sp = thread->user_sp;
//   mctx->status = thread->status;
//   mctx->errno = thread->errno;
//
//   uintptr_t fn = 0;
//   uint64_t *stack = (void *) thread->user_sp;
//   if (handler->flags & SA_SIGINFO) {
//     // void func(int signo, siginfo_t *info, void *context);
//     fn = (uintptr_t) handler->sigaction;
//
//     *stack-- = 0; // context
//     *stack-- = 0; // info
//     *stack-- = (uint64_t) sig; // signo
//   } else {
//     // void func(int signo)
//     fn = (uintptr_t) handler->handler;
//
//     *stack-- = 0; // context
//     *stack-- = 0; // info
//     *stack-- = (uint64_t) sig; // signo
//   }
//
//   thread_sighandle(fn, (uintptr_t) stack);
//   return 0;
// }
//
// int signal_getaction(pid_t pid, int sig, sigaction_t *oact) {
//   if (sig <= 0 || sig >= NSIG || sig == SIGKILL || sig == SIGSTOP) {
//     ERRNO = EINVAL;
//     return -1;
//   } else if (oact == NULL) {
//     return 0;
//   }
//
//   process_t *process = process_get(pid);
//   if (process == NULL) {
//     ERRNO = ESRCH;
//     return -1;
//   } else if (sig == 0) {
//     // null signal return success
//     return 0;
//   }
//
//   sig_handler_t *handler = process->sig_handlers[sig];
//   if (handler == NULL || handler->type != SIG_ACTION) {
//     oact->sa_flags = 0;
//     oact->sa_mask = 0;
//     oact->sa_handler = NULL;
//     oact->sa_sigaction = NULL;
//     return 0;
//   }
//
//   oact->sa_flags = handler->flags;
//   oact->sa_mask = handler->mask;
//   if (oact->sa_flags & SA_SIGINFO) {
//     oact->sa_handler = NULL;
//     oact->sa_sigaction = handler->sigaction;
//   } else {
//     oact->sa_sigaction = NULL;
//     oact->sa_handler = handler->handler;
//   }
//   return 0;
// }
//
// int signal_setaction(pid_t pid, int sig, int type, const sigaction_t *act) {
//   if (
//     sig <= 0 || sig >= NSIG || sig == SIGKILL || sig == SIGSTOP ||
//     type < SIG_DEFAULT || type > SIG_ACTION
//   ) {
//     ERRNO = EINVAL;
//     return -1;
//   }
//
//   if (act != NULL && sig >= SIGRTMIN && sig <= SIGRTMAX && !(act->sa_flags & SA_SIGINFO)) {
//     ERRNO = EINVAL;
//     return -1;
//   } else if (type == SIG_ACTION && act == NULL) {
//     return 0;
//   }
//
//   process_t *process = process_get(pid);
//   if (process == NULL) {
//     ERRNO = ESRCH;
//     return -1;
//   } else if (sig == 0) {
//     // null signal return success
//     return 0;
//   }
//
//   sig_handler_t *handler = process->sig_handlers[sig];
//   kassert(handler != NULL);
//
//   // install the new handler
//   if (type == SIG_ACTION) {
//     handler->type = SIG_ACTION;
//     handler->flags = act->sa_flags;
//     handler->mask = act->sa_mask & ~(SIGKILL | SIGSTOP);
//     if (act->sa_flags & SA_SIGINFO) {
//       handler->sigaction = act->sa_sigaction;
//     } else {
//       handler->handler = act->sa_handler;
//     }
//   } else {
//     handler->type = type;
//     handler->flags = 0;
//     handler->mask = 0;
//     handler->handler = NULL;
//   }
//
//   return 0;
// }
