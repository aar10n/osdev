//
// Created by Aaron Gill-Braun on 2023-07-07.
//

#ifndef INCLUDE_ABI_SIGNAL_H
#define INCLUDE_ABI_SIGNAL_H

typedef struct sigaltstack stack_t;
typedef int sig_atomic_t;

#define __NEED_size_t
#define __NEED_pid_t
#define __NEED_uid_t
#define __NEED_struct_timespec
#define __NEED_pthread_t
#define __NEED_pthread_attr_t
#define __NEED_time_t
#define __NEED_clock_t
#define __NEED_sigset_t
#include <bits/alltypes.h>
#include <bits/signal.h>

#define NSIG _NSIG

#define SIGRTMIN  35
#define SIGRTMAX  (_NSIG-1)
#define NRRTSIG  (SIGRTMAX-SIGRTMIN+1)

#define SIG_BLOCK     0
#define SIG_UNBLOCK   1
#define SIG_SETMASK   2

#define SI_ASYNCNL (-60)
#define SI_TKILL (-6)
#define SI_SIGIO (-5)
#define SI_ASYNCIO (-4)
#define SI_MESGQ (-3)
#define SI_TIMER (-2)
#define SI_QUEUE (-1)
#define SI_USER 0
#define SI_KERNEL 128

#define SIG_ERR  ((void (*)(int))-1)
#define SIG_DFL  ((void (*)(int)) 0)
#define SIG_IGN  ((void (*)(int)) 1)

#define FPE_INTDIV 1
#define FPE_INTOVF 2
#define FPE_FLTDIV 3
#define FPE_FLTOVF 4
#define FPE_FLTUND 5
#define FPE_FLTRES 6
#define FPE_FLTINV 7
#define FPE_FLTSUB 8

#define ILL_ILLOPC 1
#define ILL_ILLOPN 2
#define ILL_ILLADR 3
#define ILL_ILLTRP 4
#define ILL_PRVOPC 5
#define ILL_PRVREG 6
#define ILL_COPROC 7
#define ILL_BADSTK 8

#define SEGV_MAPERR 1
#define SEGV_ACCERR 2
#define SEGV_BNDERR 3
#define SEGV_PKUERR 4
#define SEGV_MTEAERR 8
#define SEGV_MTESERR 9

#define BUS_ADRALN 1
#define BUS_ADRERR 2
#define BUS_OBJERR 3
#define BUS_MCEERR_AR 4
#define BUS_MCEERR_AO 5

#define CLD_EXITED 1
#define CLD_KILLED 2
#define CLD_DUMPED 3
#define CLD_TRAPPED 4
#define CLD_STOPPED 5
#define CLD_CONTINUED 6

union sigval {
  int sival_int;
  void *sival_ptr;
};

struct siginfo {
  int si_signo;           // signal number
  int si_code;            // signal code
  union sigval si_value;  // signal value
  int si_errno;           // errno
  pid_t si_pid;           // sending process
  uid_t si_uid;           // sending user
  void *si_addr;          // faulting address
  int si_status;          // exit status
  int si_band;            // band event
};

// matches k_sigaction defined in musl/<arch>/ksigaction.h
struct sigaction {
  union {
    void (*sa_handler)(int);
    void (*sa_sigaction)(int, struct siginfo *, void *);
  };
  unsigned long sa_flags;
  void (*sa_restorer)(void);
  unsigned sa_mask[2];
};
static_assert(sizeof(struct sigaction) == 32);

/* SA_ flags in addition to ones in <bits/signal.h> */
#define SA_KERNHAND   0x02000000 // runs in kernel space

struct sigevent {
  union sigval sigev_value;
  int sigev_signo;
  int sigev_notify;
  union {
    char __pad[64 - (2*sizeof(int)) - sizeof(union sigval)];
    pid_t sigev_notify_thread_id;
    struct {
      void (*sigev_notify_function)(union sigval);
      pthread_attr_t *sigev_notify_attributes;
    } __sev_thread;
  } __sev_fields;
};

#define sigev_notify_thread_id __sev_fields.sigev_notify_thread_id
#define sigev_notify_function __sev_fields.__sev_thread.sigev_notify_function
#define sigev_notify_attributes __sev_fields.__sev_thread.sigev_notify_attributes

#define SIGEV_SIGNAL 0
#define SIGEV_NONE 1
#define SIGEV_THREAD 2
#define SIGEV_THREAD_ID 4

#endif
