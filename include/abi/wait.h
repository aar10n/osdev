//
// Created by Aaron Gill-Braun on 2023-07-07.
//

#ifndef INCLUDE_ABI_WAIT_H
#define INCLUDE_ABI_WAIT_H

typedef enum {
  P_ALL = 0,
  P_PID = 1,
  P_PGID = 2,
  P_PIDFD = 3
} idtype_t;

/* wait family and waitpid() options */
#define WNOHANG    1
#define WUNTRACED  2

/* waitid() options */
#define WSTOPPED   2
#define WEXITED    4
#define WCONTINUED 8
#define WNOWAIT    0x1000000

/* macros for working with wait status */
#define WEXITSTATUS(s) (((s) & 0xff00) >> 8)
#define WTERMSIG(s) ((s) & 0x7f)
#define WSTOPSIG(s) WEXITSTATUS(s)
#define WCOREDUMP(s) ((s) & 0x80)
#define WIFEXITED(s) (!WTERMSIG(s))
#define WIFSTOPPED(s) ((short)((((s)&0xffff)*0x10001U)>>8) > 0x7f00)
#define WIFSIGNALED(s) (((s)&0xffff)-1U < 0xffu)
#define WIFCONTINUED(s) ((s) == 0xffff)

/* macros for constructing wait statuses */
#define W_EXITCODE(ret, sig)    (((ret) << 8) | ((sig) & 0x7f))
#define W_STOPCODE(sig)         (((sig) << 8) | 0x7f)
#define W_SIGNALED_CORE(sig)    W_EXITCODE(0, W_COREDUMPED(sig))
#define W_CONTINUED             0xffff
#define W_COREDUMP              0x80


#endif
