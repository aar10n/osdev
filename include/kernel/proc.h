//
// Created by Aaron Gill-Braun on 2023-12-26.
//

#ifndef KERNEL_PROC_H
#define KERNEL_PROC_H

#include <kernel/base.h>
#include <kernel/queue.h>
#include <kernel/lock.h>
#include <kernel/signal.h>
#include <kernel/ref.h>
#include <kernel/str.h>

#include <kernel/cpu/tcb.h>

#include <abi/resource.h>

struct ftable;
struct ventry;
struct tty;

struct runqueue;
struct lockqueue;
struct waitqueue;

struct cpuset;
struct proc;
struct pstats;
struct thread;

/*
 * Credentials
 */
struct creds {
  uid_t uid;  // user id
  uid_t euid; // effective user id
  gid_t gid;  // group id
  gid_t egid; // effective group id
  _refcount;
};

/*
 * Session
 */
typedef struct session {
  pid_t sid;                          // session id
  struct tty *tty;                    // controlling tty
  char login_name[LOGIN_NAME_MAX+1];  // login name of session leader
  LIST_HEAD(struct pgroup) pgroups;   // list of process groups
} session_t;

/*
 * Process group
 */
typedef struct pgroup {
  pid_t pgid;                       // group id
  struct session *session;          // owning session
  size_t num_procs;                 // number of processes
  LIST_HEAD(struct process) procs;  // list of processes
  LIST_ENTRY(struct pgroup) list;   // pgroup list entry
} pgroup_t;

/*
 * Process
 */
typedef struct proc {
  struct address_space *space;      // address space
  struct pgroup *group;             // owning process group
  struct creds *creds;              // owner identity (ref)
  struct ventry *pwd;               // working dir vnode (ref)
  struct ftable *files;             // open file descriptors
  struct pstats *stats;             // process stats
  struct rusage *usage;             // resource usage
  struct rlimit *limit;             // resource limit
  struct sigacts *sigacts;          // signal stuff?

  pid_t pid;                        // process id
  pid_t ppid;                       // parent pid
  sigqueue_t sigqueue;              // signals waiting for a thread

  mtx_t lock;                       // process lock
  mtx_t statlock;                   // process stats lock

  size_t num_threads;               // number of threads
  LIST_HEAD(struct thread) threads; // process threads
  enum proc_state {
    PRS_EMPTY,
    PRS_ACTIVE,
    PRS_ZOMBIE,
    PRS_EXITED,
  } state;
} proc_t;

// process flags
#define PR_LEADER 0x1   // process is group leader

struct pstats {
  struct timeval start_time;
};

/*
 * Thread
 */
typedef struct thread {
  pid_t tid;                            // thread id
  uint32_t flags;                       // thread flags
  volatile mtx_t *lock;                 // thread mutex lock
  struct tcb *tcb;                      // thread context
  struct proc *proc;                    // owning process
  struct creds *creds;                  // owner identity (ref)

  struct cpuset *cpuset;                // cpu affinity set
  struct lockqueue *own_lockq;          // thread owned lockq
  struct waitqueue *own_waitq;          // thread owned waitq
  struct lock_claim_list *lock_claims;  // wait lock claim list

  enum thread_state {
    TDS_EMPTY,
    TDS_READY,
    TDS_RUNNING,
    TDS_SLEEPING,
    TDS_WAITING,
    TDS_EXITED,
  } state;

  uintptr_t kstack_base;                // kernel stack base
  size_t kstack_size;                   // kernel stack size
  uint16_t pri_base;                    // base priority
  uint16_t priority;                    // current priority

  /* starts zeroed */
  str_t name;                           // thread name
  struct timeval start_time;            // thread start time
  struct rusage usage;                  // resource usage
  struct rlimit limit;                  // resource limit
  uint64_t runtime;                     // total runtime (ticks)
  uint64_t inc_runtime;                 // ticks to add to process

  uint32_t lock_count;                  // number of normal mutexes held
  uint32_t spin_count;                  // number of spin mutexes held
  int crit_level;                       // critical section level
  int intr_level;                       // nested interrupt level

  int errno;                            // last syscall errno
  sigset_t sigmask;                     // signal mask
  stack_t sigstack;                     // signal stack

  LIST_HEAD(struct lockqueue) lockqs;   // contested locks
  struct lockqueue *lockq;              // lock queue (if acquired/contended)
  int lockq_num;                        // lockq queue number (LQ_EXCL or LQ_SHRD)
  struct lock_object *lock_obj;         // contended lock object
  const void *wchan;                    // wait channel (if in waitqueue)
  const char *wdmsg;                    // wait debug message

  LIST_ENTRY(struct thread) plist;      // process thread list entry
  LIST_ENTRY(struct thread) rqlist;     // runq list entry
  LIST_ENTRY(struct thread) lqlist;     // lockq list entry
  LIST_ENTRY(struct thread) wqlist;     // waitq list entry
} thread_t;

#define TDF_KTHREAD   0x01 // kernel thread
#define TDF_ITHREAD   0x02 // interrupt thread
#define TDF_IDLE      0x04 // per-cpu idle thread

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

void proc_init();

pid_t proc_alloc_pid();
void proc_free_pid(pid_t pid);
proc_t *proc_alloc_empty(pid_t pid, struct address_space *space, pgroup_t *group, struct creds *creds);
void proc_free_exited(proc_t **procp);
void   proc_setup_add_thread(proc_t *proc, thread_t *td);
// void   proc_setup_runq(proc_t *proc, );

thread_t *thread_alloc_empty(uint32_t flags, proc_t *proc, struct creds *creds);
void thread_free_exited(thread_t **tdp);
void   thread_setup_kstack(thread_t *td, uintptr_t base, size_t size);
void   thread_setup_priority(thread_t *td, uint8_t base_pri);

void thread_lock(thread_t *td);



struct cpuset *cpuset_alloc(struct cpuset *existing);
void cpuset_free(struct cpuset *set);
void cpuset_set(struct cpuset *set, int cpu);
void cpuset_reset(struct cpuset *set, int cpu);
bool cpuset_test(struct cpuset *set, int cpu);
int cpuset_next_set(struct cpuset *set, int cpu);

void critical_enter();
void critical_exit();

#endif
