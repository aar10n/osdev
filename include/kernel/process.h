//
// Created by Aaron Gill-Braun on 2020-10-17.
//

#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H

#include <kernel/base.h>
#include <kernel/queue.h>
#include <kernel/ref.h>
#include <kernel/mutex.h>
#include <kernel/signal.h>
#include <kernel/mm_types.h>

#include <kernel/cpu/tcb.h>

#include <abi/signal.h>
#include <abi/resource.h>

#include <atomic.h>

#define MAX_PROCS 1024


struct ftable;
struct ventry;
struct tty;

struct pgroup;
struct process;
struct thread;
struct cpuset;
struct creds;


/*
 * Session
 */
typedef struct session {
  pid_t sid;                        // session id
  struct tty *tty;                  // controlling tty
  str_t login_name;                 // login name of session leader

  LIST_HEAD(struct pgroup) pgroups;
  _refcount;
} session_t;


/*
 * Process group
 */
typedef struct pgroup {
  pid_t pgid;                       // group id
  struct session *session;          // owning session (ref)

  mutex_t lock;                     // pgroup mutex
  _refcount;

  size_t nprocs;                    // number of processes
  LIST_HEAD(struct process) procs;  // list of processes

  LIST_ENTRY(struct pgroup) list;   // pgroup list entry
} pgroup_t;


/*
 * Process
 */
typedef struct process {
  pid_t pid;                        // process id
  pid_t ppid;                       // parent pid
  struct address_space *space;      // address space
  struct ftable *files;             // open file descriptors
  struct creds *creds;              // owner identity (ref)
  struct ventry *pwd;               // working dir vnode (ref)
  struct pgroup *group;             // owning process group (ref)

  enum proc_state {
    PRS_ACTIVE,
    PRS_ZOMBIE,
    PRS_EXITED,
  } state;

  mutex_t lock;                     // process lock
  spinlock_t usage_lock;            // usage stats lock

  struct thread *main;              // main thread
  LIST_HEAD(struct thread) threads; // process threads
  size_t nthreads;                  // number of threads

  struct timeval start_time;        // process start time
  struct rusage usage;              // accumulated rusage
  uint64_t total_runtime;           // total process runtime in ns
                                    //    (updated atomically - no lock)

  sigqueue_t sigqueue;              // signals waiting for a thread
  uintptr_t brk_base;               // brk base vaddr
  size_t brk_size;                  // brk vm size

  LIST_HEAD(struct process) children; // list of child processes
  LIST_ENTRY(struct process) siblings;// process group siblings
  _refcount;
} process_t;

// process flags
#define PR_LEADER 0x1   // process is group leader


/*
 * Thread
 */
typedef struct thread {
  pid_t tid;                    // thread id
  uint32_t flags;               // thread flags
  struct tcb *tcb;              // thread context
  struct process *process;      // associated process (ref)
  struct creds *creds;          // owner identity (ref)
  struct cpuset *cpuset;        // cpu affinity mask

  mutex_t lock;                 // thread mutex
  spinlock_t stats_lock;        // stats+usage lock

  enum thread_state {
    TDS_READY,
    TDS_RUNNING,
    TDS_BLOCKED,
    TDS_SLEEPING,
    TDS_KILLED,
  } state;

  uint8_t policy;               // thread policy
  uint8_t priority;             // thread priority
  uint8_t cpu_id;               // current cpu id
  uint8_t last_cpu_id;          // last active cpu id
  clockid_t alarm_id;           // alarm id (if sleeping)
  clock_t sleep_until;          // wakeup time (if sleeping)

  str_t name;                   // thread name
  struct timeval start_time;    // thread start time
  struct rusage usage;          // resource usage
  struct thread_stats {
    uint64_t runtime;           // total runtime (ns)
    uint64_t last_active;       // last time active (ns)
    uint64_t last_scheduled;    // last time scheduled (ns)

    uint64_t switches;          // number of context switches
    uint64_t preempted;         // number of preemptions
    uint64_t blocks;            // number of blocks
    uint64_t sleeps;            // number of sleeps
    uint64_t yields;            // number of yields
    void *data;                 // policy private data
  } stats;

  uintptr_t stack_base;         // kernel stack base vaddr
  size_t stack_size;            // kernel stack size

  const void *wlock_ptr;        // pointer to the 'actual' blocking lock (if blocked)
  const mutex_t *wlock_mtx;     // pointer to the blocking mutex (if blocked)
  const char *wlock_reason;     // reason for waiting on lock (if blocked)
  int critical_level;           // number of nested critical sections
  int intr_level;               // number of nested interrupts

  LIST_ENTRY(struct thread) list;   // ready/blocked thread list
  LIST_ENTRY(struct thread) group;  // thread group
  _refcount;
} thread_t;

// thread flags
#define TD_KTHREAD    0x1 // kernel thread
#define TD_IDLE       0x2 // per-cpu idle thread
#define TD_EXITING    0x4 // thread is exiting

#define td_begin_critical(td) (atomic_fetch_add(&(td)->critical_level, 1))
#define td_end_critical(td) (atomic_fetch_sub(&(td)->critical_level, 1))


#define LOCK_TD(td) (mutex_lock(&(td)->lock))
#define UNLOCK_TD(td) (mutex_unlock(&(td)->lock))
#define LOCK_TD_STATS(td) (spin_lock(&(td)->stats_lock))
#define UNLOCK_TD_STATS(td) (spin_unlock(&(td)->stats_lock))



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


struct cpuset *cpuset_copy(struct cpuset *set);
void cpuset_free(struct cpuset *set);
void cpuset_set(struct cpuset *set, int cpu);
void cpuset_reset(struct cpuset *set, int cpu);
bool cpuset_test(struct cpuset *set, int cpu);
int cpuset_next_set(struct cpuset *set, int cpu);


/* vfork flags */
#define F_COPY_FDS      0x1 // copy file descriptor table
#define F_SHARE_FDS     0x2 // share file descriptor table (excl. with F_COPY_FDS)
#define F_COPY_SIGACTS  0x4 // clone signal handlers

void proc_init();
pid_t proc_fork(uint32_t f_flags);
// pid_t proc_kcreate();

#endif
