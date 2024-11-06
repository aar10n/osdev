//
// Created by Aaron Gill-Braun on 2023-12-26.
//

#ifndef KERNEL_PROC_H
#define KERNEL_PROC_H

#include <kernel/base.h>
#include <kernel/queue.h>
#include <kernel/mutex.h>
#include <kernel/cond.h>
#include <kernel/signal.h>
#include <kernel/ref.h>
#include <kernel/str.h>

#include <kernel/cpu/tcb.h>
#include <kernel/cpu/frame.h>

#include <abi/resource.h>

struct page;
struct ftable;
struct ventry;
struct tty;

struct runqueue;
struct lockqueue;
struct waitqueue;

struct cpuset;
struct proc;
struct pstats;
struct pstrings;
struct thread;

/*
 * Session
 */
typedef struct session {
  pid_t sid;                          // session id
  mtx_t lock;                         // session mutex
  struct tty *tty;                    // controlling tty
  char login_name[LOGIN_NAME_MAX+1];  // login name of session leader

  size_t num_pgroups;                 // number of process groups
  LIST_HEAD(struct pgroup) pgroups;   // list of process groups
} session_t;

/*
 * Process group
 */
typedef struct pgroup {
  pid_t pgid;                       // pgroup id
  mtx_t lock;                       // pgroup mutex
  struct session *session;          // owning session

  size_t num_procs;                 // number of processes
  LIST_HEAD(struct proc) procs;     // process list

  LIST_ENTRY(struct pgroup) sslist; // session list entry
  LIST_ENTRY(struct pgroup) hashlist;// pgtable hash list entry
} pgroup_t;

#define pgrp_lock_assert(pg, what) __type_checked(pgroup_t*, pg, mtx_assert(&(pg)->lock, what))
#define pgrp_lock(pg) __type_checked(pgroup_t*, pg, mtx_lock(&(pg)->lock))
#define pgrp_unlock(pg) __type_checked(pgroup_t*, pg, mtx_unlock(&(pg)->lock))

/*
 * Process
 */
typedef struct proc {
  pid_t pid;                        // process id
  uint32_t flags;                   // process flags
  struct address_space *space;      // address space
  struct pgroup *group;             // owning process group
  struct pcreds *creds;             // owner credentials (ref)
  struct ventry *pwd;               // working dir vnode (ref)

  struct ftable *files;             // open file descriptors
  struct sigacts *sigacts;          // signal stuff?
  struct rusage *usage;             // resource usage
  struct rlimit *limit;             // resource limit
  struct pstats *stats;             // process stats

  enum proc_state {
    PRS_EMPTY,
    PRS_ACTIVE,
    PRS_ZOMBIE,
    PRS_EXITED,
  } state;

  mtx_t lock;                       // process lock
  mtx_t statlock;                   // process stats lock
  cond_t td_exit_cond;              // thread exit condition

  /* starts zeroed */
  struct pstrings *args;            // process arguments
  struct pstrings *env;             // process environment
  str_t binpath;                    // process executable path

  uintptr_t brk_start;              // process brk segment start
  uintptr_t brk_end;                // process brk segment end
  uintptr_t brk_max;                // process brk max address

  str_t name;                       // process name
  sigqueue_t sigqueue;              // signals waiting for a thread
  pid_t ppid;                       // parent pid

  int exit_code;                    // process exit code
  volatile uint32_t num_exiting;    // pending thread exits
  volatile uint32_t num_exited;     // number of exited threads
  uint32_t num_threads;             // number of threads
  LIST_HEAD(struct thread) threads; // process threads

  LIST_ENTRY(struct proc) pglist;   // process group list entry
  LIST_ENTRY(struct proc) hashlist; // ptable hash list entry
} proc_t;

// process flags
#define PRF_LEADER 0x1   // process is group leader
#define   PRF_IS_LEADER(p) ((p)->flags & PRF_LEADER)
#define PRF_HASRUN 0x2   // process has run at least once
#define   PRF_HAS_RUN(p) ((p)->flags & PRF_HASRUN)

#define PRS_IS_EMPTY(p) ((p)->state == PRS_EMPTY)
#define PRS_IS_ALIVE(p) ((p)->state == PRS_ACTIVE)
#define PRS_IS_ZOMBIE(p) ((p)->state == PRS_ZOMBIE)
#define PRS_IS_EXITED(p) ((p)->state == PRS_EXITED)

#define pr_main_thread(p) LIST_FIRST(&(p)->threads)
#define pr_lock_assert(p, what) __type_checked(proc_t*, p, mtx_assert(&(p)->lock, what))
#define pr_lock(p) __type_checked(proc_t*, p, mtx_lock(&(p)->lock))
#define pr_unlock(p) __type_checked(proc_t*, p, mtx_unlock(&(p)->lock))

struct pstats {
  struct timeval start_time;
};

struct pcreds {
  uid_t uid;  // user id
  uid_t euid; // effective user id
  gid_t gid;  // group id
  gid_t egid; // effective group id
  _refcount;
};

struct pstrings {
  uint32_t count;       // number of strings
  uint32_t size;        // size of all strings
  struct page *pages;   // pages containing the strings (ref)
  char *kptr;           // kernel pointer to the strings
};

/*
 * Thread
 */
typedef struct thread {
  pid_t tid;                            // thread id
  uint32_t flags;                       // thread flags
  mtx_t lock;                           // thread mutex lock
  struct tcb *tcb;                      // thread context
  struct proc *proc;                    // owning process
  struct trapframe *frame;              // thread trapframe
  uintptr_t kstack_base;                // kernel stack base
  size_t kstack_size;                   // kernel stack size

  struct pcreds *creds;                 // owner identity (ref)
  struct cpuset *cpuset;                // cpu affinity set
  struct lockqueue *own_lockq;          // thread owned lockq
  struct waitqueue *own_waitq;          // thread owned waitq
  struct lock_claim_list *wait_claims;  // wait lock claim list

  enum thread_state {
    TDS_EMPTY,    // thread is being set up
    TDS_READY,    // thread is on a runqueue
    TDS_RUNNING,  // thread is running on a cpu
    TDS_BLOCKED,  // thread is on a lockqueue
    TDS_WAITING,  // thread is on a waitqueue
    TDS_EXITED,   // thread has exited
  } state;

  int cpu_id;                           // last cpu thread ran on
  volatile uint32_t flags2;             // private flags
  uint16_t : 16;
  uint8_t pri_base;                     // base priority
  uint8_t priority;                     // current priority

  /* starts zeroed */
  str_t name;                           // thread name
  struct timeval start_time;            // thread start time
  struct rusage usage;                  // resource usage
  struct rlimit limit;                  // resource limit
  uint64_t runtime;                     // total run time
  uint64_t blocktime;                   // total block time

  int lock_count;                       // number of normal mutexes held
  int spin_count;                       // number of spin mutexes held
  int crit_level;                       // critical section level

  int errno;                            // last thread errno
  sigset_t sigmask;                     // signal mask
  stack_t sigstack;                     // signal stack

  struct runqueue *runq;                // runqueue (if ready)
  struct lock_object *lockobj;          // contested lock (if blocked)
  int lockq_num;                        // lockq queue number (LQ_EXCL or LQ_SHRD)
  const void *wchan;                    // wait channel (if in waitqueue)
  const char *wdmsg;                    // wait debug message

  LIST_ENTRY(struct thread) plist;      // process thread list entry
  LIST_ENTRY(struct thread) rqlist;     // runq list entry
  LIST_ENTRY(struct thread) lqlist;     // lockq list entry
  LIST_ENTRY(struct thread) wqlist;     // waitq list entry
} thread_t;
static_assert(offsetof(thread_t, kstack_base) == 0x38);
static_assert(offsetof(thread_t, kstack_size) == 0x40);

// thread flags
#define TDF_KTHREAD     0x00000001  // kernel thread
#define   TDF_IS_KTHREAD(td) ((td)->flags & TDF_KTHREAD)
#define TDF_ITHREAD     0x00000002  // interrupt thread
#define   TDF_IS_ITHREAD(td) ((td)->flags & TDF_ITHREAD)
#define TDF_IDLE        0x00000004  // per-cpu idle thread
#define   TDF_IS_IDLE(td) ((td)->flags & TDF_IDLE)

// private thread flags
#define TDF2_STOPPING   0x00000001  // thread has been marked for exit
#define   TDF2_IS_STOPPING(td) ((td)->flags2 & TDF2_STOPPING)
#define TDF2_FIRSTTIME  0x00000002  // thread has not yet run
#define   TDF2_IS_FIRSTTIME(td) ((td)->flags2 & TDF2_FIRSTTIME)
#define TDF2_AFFINITY   0x00000004  // thread has cpu affinity
#define   TDF2_HAS_AFFINITY(td) ((td)->flags2 & TDF2_AFFINITY)
#define TDF2_INTRP      0x00000008  // thread was interrupted
#define   TDF2_WAS_INTRP(td) ((td)->flags2 & TDF2_INTRP)

#define TDS_IS_EMPTY(td) ((td)->state == TDS_EMPTY)
#define TDS_IS_READY(td) ((td)->state == TDS_READY)
#define TDS_IS_RUNNING(td) ((td)->state == TDS_RUNNING)
#define TDS_IS_BLOCKED(td) ((td)->state == TDS_BLOCKED)
#define TDS_IS_WAITING(td) ((td)->state == TDS_WAITING)
#define TDS_IS_EXITED(td) ((td)->state == TDS_EXITED)

#define TD_CPU_ID()

#define TD_SET_STATE(td, s) ((td)->state = (s))
#define TD_SET_CPU(td, id) ((td)->)

#define td_lock_assert(td, what) __type_checked(thread_t*, td, mtx_assert(&(td)->lock, what))
#define td_lock(td) _thread_lock(td, __FILE__, __LINE__)
#define td_unlock(td) _thread_unlock(td, __FILE__, __LINE__)

// realtime threads: 48-79
// timeshare threads: 120-223
// idle threads: 224-255

#define PRI_REALTIME  48
#define PRI_NORMAL    120
#define PRI_IDLE      224

#define TD_IS_REALTIME(td) ((td)->priority >= PRI_REALTIME && (td)->priority < PRI_NORMAL)
#define TD_IS_TIMESHARE(td) ((td)->priority >= PRI_NORMAL && (td)->priority < PRI_IDLE)
#define TD_IS_IDLE(td) ((td)->priority >= PRI_IDLE)

//
//
//

void proc0_init();

__move struct pcreds *pcreds_alloc(uid_t uid, gid_t gid);
void pcreds_release(__move struct pcreds **credsp);

pgroup_t *pgrp_alloc_empty(pid_t pgid, session_t *session);
void pgrp_free_empty(pgroup_t **pgp);
void   pgrp_setup_add_proc(pgroup_t *pg, proc_t *proc);
void pgrp_add_proc(pgroup_t *pg, proc_t *proc);
void pgrp_remove_proc(pgroup_t *pg, proc_t *proc);

pid_t proc_alloc_pid();
void proc_free_pid(pid_t pid);

proc_t *proc_alloc_empty(pid_t pid, struct address_space *space, struct pcreds *creds);
void proc_free_exited(proc_t **procp);
void   proc_setup_add_thread(proc_t *proc, thread_t *td);
int    proc_setup_new_env(proc_t *proc, const char *const env[]);
int    proc_setup_copy_env(proc_t *proc, struct pstrings *env);
int    proc_setup_exec_args(proc_t *proc, const char *const args[]);
int    proc_setup_exec(proc_t *proc, cstr_t path);
int    proc_setup_clone_fds(proc_t *proc, struct ftable *ftable);
int    proc_setup_open_fd(proc_t *proc, int fd, cstr_t path, int flags);
void   proc_finish_setup_and_submit_all(proc_t *proc);
void proc_add_thread(proc_t *proc, thread_t *td);
void proc_exit_all_wait(proc_t *proc, int exit_code);

thread_t *thread_alloc_idle();
thread_t *thread_alloc_empty(uint32_t flags, uintptr_t kstack_base, size_t kstack_size);
static inline uintptr_t thread_get_kstack_top(thread_t *td) { return (uintptr_t) td->frame; }
void thread_free_exited(thread_t **tdp);
void   thread_setup_entry(thread_t *td, uintptr_t entry, uintptr_t arg);
void   thread_setup_priority(thread_t *td, uint8_t base_pri);
void   thread_finish_setup_and_submit(thread_t *td);
void thread_stop(thread_t *td);

struct cpuset *cpuset_alloc(struct cpuset *existing);
void cpuset_free(struct cpuset **set);
void cpuset_set(struct cpuset *set, int cpu);
void cpuset_reset(struct cpuset *set, int cpu);
bool cpuset_test(struct cpuset *set, int cpu);
int cpuset_next_set(struct cpuset *set, int cpu);

void critical_enter();
void critical_exit();

#endif
