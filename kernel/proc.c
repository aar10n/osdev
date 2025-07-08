//
// Created by Aaron Gill-Braun on 2023-12-26.
//

#include <kernel/proc.h>
#include <kernel/sched.h>
#include <kernel/tqueue.h>
#include <kernel/alarm.h>
#include <kernel/exec.h>
#include <kernel/mm.h>
#include <kernel/fs.h>
#include <kernel/signal.h>
#include <kernel/tty.h>

#include <kernel/printf.h>
#include <kernel/panic.h>
#include <kernel/bits.h>
#include <kernel/str.h>

#include <kernel/cpu/cpu.h>
#include <kernel/debug/debug.h>
#include <kernel/vfs/file.h>
#include <kernel/vfs/ventry.h>

#include <bitmap.h>

noreturn void idle_thread_entry();

extern uintptr_t entry_initial_stack;
extern uintptr_t entry_initial_stack_top;

extern void kernel_thread_entry();

// #define ASSERT(x)
#define ASSERT(x) kassert(x)
#define ASSERTF(x, fmt, ...) kassertf(x, fmt, ##__VA_ARGS__)
// #define DPRINTF(...)
#define DPRINTF(x, ...) kprintf("proc: " x, ##__VA_ARGS__)
#define EPRINTF(x, ...) kprintf("proc: %s: " x, __func__, ##__VA_ARGS__)

#define goto_res(lbl, err) do { res = err; goto lbl; } while (0)

#define PROCS_MAX     1024
#define PROC_BRK_MAX  SIZE_16MB

#define PROC_USTACK_BASE 0x8000000
#define PROC_USTACK_SIZE SIZE_16KB

proc_t *proc_alloc_internal(
  pid_t pid,
  struct address_space *space,
  struct pcreds *creds,
  struct ventry *pwd,
  bool fork
);

static struct pcreds *root_creds;
static pgroup_t *pgroup0;
static proc_t *proc0;
static mtx_t proc0_ap_lock;

struct percpu *percpu0;

void proc0_init() {
  // runs just after the early initializers
  percpu0 = curcpu_area;

  root_creds = pcreds_alloc(0, 0);
  proc0 = proc_alloc_internal(0, NULL, getref(root_creds), fs_root_getref(), /*fork=*/false);
  pgroup0 = pgrp_alloc_add_proc(proc0);

  // this lock is used to provide exclusive access to proc0 among the APs while they're
  // initializing and attaching their idle threads. they wont have a proc context at that
  // point therefore cant use pr_lock/_unlock.
  mtx_init(&proc0_ap_lock, MTX_SPIN, "proc0_ap_lock");

  thread_t *maintd = thread_alloc_proc0_main();
  maintd->name = str_from("proc0_main");
  proc_setup_add_thread(proc0, maintd);

  proc0->state = PRS_ACTIVE;
  proc0->name = str_from("proc0");

  set_curthread(maintd);
  set_curproc(proc0);
  set_kernel_sp(thread_get_kstack_top(maintd));
  set_tss_rsp0_ptr(offset_addr(maintd->frame, sizeof(struct trapframe)));
  // we cant insert proc0 into the ptable yet since it hasnt been initialized
}

/////////////////
// MARK: ptable

struct ptable_entry {
  mtx_t lock;
  LIST_HEAD(struct proc) head;
};

static struct ptable {
  struct ptable_entry *entries;
  size_t nprocs; // number of processes
} _ptable;

#define pid_index(pid) ((pid) % PROCS_MAX)

static inline __ref proc_t *ptable_get_proc(pid_t pid) {
  ASSERT(pid < PROCS_MAX);
  struct ptable_entry *entry = &_ptable.entries[pid_index(pid)];
  mtx_spin_lock(&entry->lock);
  LIST_FOR_IN(proc, &entry->head, hashlist) {
    if (proc->pid == pid) {
      mtx_spin_unlock(&entry->lock);
      return pr_getref(proc);
    }
  }

  mtx_spin_unlock(&entry->lock);
  return NULL;
}

static inline void ptable_add_proc(pid_t pid, __ref proc_t *proc) {
  ASSERT(pid < PROCS_MAX);
  struct ptable_entry *entry = &_ptable.entries[pid_index(pid)];
  mtx_spin_lock(&entry->lock);
  LIST_INSERT_ORDERED_BY(&entry->head, moveref(proc), hashlist, pid);
  atomic_fetch_add(&_ptable.nprocs, 1);
  mtx_spin_unlock(&entry->lock);
}

static inline void ptable_remove_proc(pid_t pid, __ref proc_t *proc) {
  ASSERT(proc->state == PRS_EXITED);
  ASSERT(pid < PROCS_MAX);
  struct ptable_entry *entry = &_ptable.entries[pid_index(pid)];
  mtx_spin_lock(&entry->lock);
  proc_t *tmp = LIST_REMOVE(&entry->head, proc, hashlist);
  pr_putref(&tmp);
  atomic_fetch_sub(&_ptable.nprocs, 1);
  mtx_spin_unlock(&entry->lock);
  pr_putref(&proc);
}

static void ptable_static_init() {
  size_t ptable_size = page_align(PROCS_MAX * sizeof(struct ptable_entry));
  uintptr_t ptable_base = vmap_anon(0, 0, ptable_size, VM_WRITE|VM_GLOBAL, "ptable");
  kassert(ptable_base != 0);

  _ptable.entries = (struct ptable_entry *) ptable_base;
  _ptable.nprocs = 0;
  for (int i = 0; i < PROCS_MAX; i++) {
    mtx_init(&_ptable.entries[i].lock, MTX_SPIN, "ptable_entry_lock");
    LIST_INIT(&_ptable.entries[i].head);
  }

  ptable_add_proc(0, pr_getref(proc0));
}
STATIC_INIT(ptable_static_init);

//////////////////
// MARK: pid set

static bitmap_t *_pidset; // allocatable pid set
static mtx_t _pidset_lock; // pid set lock

static void pidset_static_init() {
  _pidset = create_bitmap(PROCS_MAX);
  bitmap_set(_pidset, 0); // pid 0 is reserved
  mtx_init(&_pidset_lock, MTX_SPIN, "pidset_lock");
}
STATIC_INIT(pidset_static_init);

static pid_t pidset_alloc() {
  mtx_spin_lock(&_pidset_lock);
  pid_t res = (pid_t) bitmap_get_set_free(_pidset);
  mtx_spin_unlock(&_pidset_lock);
  return res;
}

static void pidset_free(pid_t pid) {
  bitmap_clear(_pidset, pid);
}

///////////////////
// MARK: pcreds

__ref struct pcreds *pcreds_alloc(uid_t uid, gid_t gid) {
  struct pcreds *creds = kmallocz(sizeof(struct pcreds));
  creds->uid = uid;
  creds->gid = gid;
  creds->euid = uid;
  creds->egid = gid;
  ref_init(&creds->refcount);
  return creds;
}

void pcreds_cleanup(__move struct pcreds **credsp) {
  struct pcreds *creds = moveref(*credsp);
  if (creds == NULL)
    return;

  kfreep(&creds);
}

//////////////////
// MARK: pstrings

static int pstrings_alloc(const char *const strings[], int limit, struct pstrings **out) {
  ASSERT(out != NULL);
  size_t count = 0;
  size_t full_size = 0;
  page_t *pages = NULL;
  void *kptr = NULL;
  int res = 0;

  if (strings == NULL) {
    goto finish;
  }

  while (strings[count] != NULL) {
    full_size += strlen(strings[count]) + 1;
    count++;
  }

  if (count == 0) {
    goto finish;
  } else if (count > limit) {
    DPRINTF("pstrings_alloc: too many strings (%zu > %d)\n", count, limit);
    res = -E2BIG;
    goto finish;
  } else if (full_size > SIZE_1MB) {
    DPRINTF("pstrings_alloc: strings too large\n");
    res = -E2BIG;
    goto finish;
  }

  pages = alloc_pages(SIZE_TO_PAGES(full_size));
  if (pages == NULL) {
    DPRINTF("pstrings_alloc: failed to allocate pages\n");
    res = -ENOMEM;
    goto finish;
  }

  size_t aligned_size = page_align(full_size);
  kptr = (void *) vmap_pages(getref(pages), 0, aligned_size, VM_RDWR, "pstrings");
  if (kptr == NULL) {
    DPRINTF("pstrings_alloc: failed to map pages\n");
    drop_pages(&pages);
    res = -ENOMEM;
    goto finish;
  }

LABEL(finish);
  if (res < 0) {
    DPRINTF("pstrings_alloc: failed to allocate pages\n");
    return res;
  }

  struct pstrings *pstrings = kmallocz(sizeof(struct pstrings));
  pstrings->count = count;
  pstrings->size = full_size;
  pstrings->pages = pages;
  pstrings->kptr = kptr;

  // copy the strings into the allocated space
  char *pstr = kptr;
  for (size_t i = 0; i < count; i++) {
    const char *str = strings[i];
    while (*str != '\0') {
      *pstr = *str;
      pstr++;
      str++;
    }
    *pstr = '\0';
    pstr++;
  }

  *out = pstrings;
  return 0;
}

static struct pstrings *pstrings_copy(struct pstrings *pstrings) {
  if (pstrings == NULL)
    return NULL;

  page_t *pages = alloc_cow_pages(pstrings->pages);
  if (pages == NULL) {
    DPRINTF("pstrings_copy: failed to allocate pages\n");
    return NULL;
  }

  size_t aligned_size = page_align(pstrings->size);
  void *kptr = (void *) vmap_pages(getref(pages), 0, aligned_size, VM_RDWR, "pstrings");
  if (kptr == NULL) {
    DPRINTF("pstrings_copy: failed to map pages\n");
    drop_pages(&pages);
    return NULL;
  }

  memcpy(kptr, pstrings->kptr, pstrings->size);

  struct pstrings *copy = kmallocz(sizeof(struct pstrings));
  copy->count = pstrings->count;
  copy->size = pstrings->size;
  copy->pages = pages;
  copy->kptr = kptr;
  return copy;
}

static void pstrings_free(struct pstrings **pstringp) {
  struct pstrings *pstrings = *pstringp;
  if (pstrings->kptr != NULL) {
    vmap_free((uintptr_t) pstrings->kptr, page_align(pstrings->size));
    pstrings->kptr = NULL;
  }

  drop_pages(&pstrings->pages);
  kfree(pstrings);
}

///////////////////
// MARK: session

__move session_t *session_alloc(pid_t sid) {
  session_t *sess = kmallocz(sizeof(session_t));
  sess->sid = sid;
  sess->leader = NULL; // will be set later
  sess->tty = NULL; // will be set later
  sess->num_pgroups = 0;
  LIST_INIT(&sess->pgroups);
  mtx_init(&sess->lock, MTX_RECURSIVE, "session_lock");
  ref_init(&sess->refcount);
  return sess;
}

void session_cleanup(__move session_t **sessref) {
  session_t *sess = moveref(*sessref);
  ASSERT(sess->refcount == 0);
  ASSERT(sess->num_pgroups == 0);
  mtx_destroy(&sess->lock);
  pr_putref(&sess->leader);
  kfree(sess);
}

void session_add_pgroup(session_t *sess, pgroup_t *pg) {
  ASSERT(pg->session == NULL);

  pg->session = sess_getref(sess);
  LIST_ADD(&sess->pgroups, pgrp_getref(pg), sslist);
  sess->num_pgroups++;
  if (sess->leader == NULL) {
    sess->leader = pgrp_get_leader(pg);
  }
}

int session_leader_ctty(session_t *sess, struct tty *tty) {
  sess_lock_assert(sess, MA_NOTOWNED);
  tty_assert_owned(tty);

  int res;
  sess_lock(sess);
  if (tty != NULL) {
    // set the controlling tty
    if (sess->tty != NULL) {
      EPRINTF("session %d already has a controlling tty\n", sess->sid);
      goto_res(ret_unlock, -EPERM);
    } else if (tty->session != NULL) {
      EPRINTF("tty is already associated with a session\n");
      goto_res(ret_unlock, -EPERM);
    }

    sess->tty = tty;
    tty->pgrp = pgrp_getref(sess->leader->group);
    tty->session = sess_getref(sess);
  } else {
    // clear the controlling tty
    if (sess->tty == NULL) {
      EPRINTF("session %d does not have a controlling tty\n", sess->sid);
      goto_res(ret_unlock, -ENOTTY);
    }

    sess->tty = NULL;
    pgrp_putref(&tty->pgrp);
    sess_putref(&tty->session);
  }

  res = 0; // success
LABEL(ret_unlock);
  sess_unlock(sess);
  return res;
}

///////////////////
// MARK: pgroup

__ref pgroup_t *pgrp_alloc_add_proc(proc_t *proc) {
  pid_t pgid = proc->pid; // pgid == pid for the leader process
  pgroup_t *pgroup = kmallocz(sizeof(pgroup_t));
  pgroup->pgid = pgid;
  pgroup->num_procs = 0;
  LIST_INIT(&pgroup->procs);
  mtx_init(&pgroup->lock, 0, "pgroup_lock");
  ref_init(&pgroup->refcount);

  LIST_ADD(&pgroup->procs, proc, pglist);
  pgroup->num_procs++;
  proc->group = pgrp_getref(pgroup);

  session_t *sess = session_alloc(pgid);
  sess->leader = pr_getref(proc);
  session_add_pgroup(sess, pgroup);
  sess_putref(&sess);
  return pgroup;
}

void pgrp_cleanup(pgroup_t **pgrpref) {
  pgroup_t *pgroup = moveref(*pgrpref);
  ASSERT(pgroup->refcount == 0);
  ASSERT(pgroup->num_procs == 0);
  sess_putref(&pgroup->session);
  mtx_destroy(&pgroup->lock);
  kfree(pgroup);
  *pgrpref = NULL;
}

__ref proc_t *pgrp_get_leader(pgroup_t *pg) {
  proc_t *leader = LIST_FIND(_proc, &pg->procs, pglist, _proc->pid == pg->pgid);
  if (leader != NULL) {
    return pr_getref(leader);
  }
  return NULL; // no leader found
}

void pgrp_add_proc(pgroup_t *pg, proc_t *proc) {
  pgrp_lock_assert(pg, MA_OWNED);

  LIST_ADD(&pg->procs, proc, pglist);
  pg->num_procs++;
  proc->group = pgrp_getref(pg);
}

void pgrp_remove_proc(pgroup_t *pg, proc_t *proc) {
  pgrp_lock_assert(pg, MA_OWNED);
  pr_lock_assert(proc, MA_OWNED);

  pgrp_putref(&proc->group);
  pg->num_procs--;
  LIST_REMOVE(&pg->procs, proc, pglist);
}

int pgrp_signal(pgroup_t *pg, int sig, int si_code, union sigval si_value) {
  pgrp_lock_assert(pg, MA_OWNED);
  ASSERT(sig >= 0 && sig < NSIG);

  int res = 0;
  LIST_FOR_IN(proc, &pg->procs, pglist) {
    pr_lock(proc);
    if (proc->state == PRS_EXITED || proc->state == PRS_ZOMBIE) {
      pr_unlock(proc);
      continue; // skip exited or zombie processes
    }

    res = proc_signal(proc, sig, si_code, si_value);
    pr_unlock(proc);
    if (res < 0) {
      EPRINTF("failed to signal process {:pr}: {:err}\n", &proc, res);
      return res;
    }
  }
  return res;
}

///////////////////
// MARK: proc

static inline void proc_do_add_thread(proc_t *proc, thread_t *td) {
  ASSERT(TDS_IS_EMPTY(td));
  td->proc = pr_getref(proc);
  td->creds = getref(proc->creds);
  td->tid = (pid_t)proc->num_threads;
  LIST_ADD(&proc->threads, td, plist);
  proc->num_threads++;
}

static inline void proc_do_remove_thread(proc_t *proc, thread_t *td) {
  ASSERT(td->proc == proc);
  // we maintain the process and pcreds references in the thread
  // until the thread is cleaned up and freed
  LIST_REMOVE(&proc->threads, td, plist);
  proc->num_threads--;
}

static inline enum proc_state proc_get_exit_state(proc_t *proc) {
  // when exiting a process will enter the PRS_EXITED state if
  // its parent has a SIGCHLD handler installed. This will mean
  // that the process is reaped immediately after all threads
  // exit. Otherwise it enters the PRS_ZOMBIE state and is
  // kept around until a process waits for it.
  if (proc->parent != NULL) {
    proc_t *parent = proc->parent;
    struct sigacts *sigacts = parent->sigacts;
    mtx_lock(&sigacts->lock);
    bool sigchld_handler =
      (sigacts->std_actions[SIGCHLD].sa_handler != SIG_IGN) &&
      (sigacts->std_actions[SIGCHLD].sa_flags & SA_NOCLDWAIT) != 0;
    mtx_unlock(&sigacts->lock);
    if (sigchld_handler) {
      return PRS_ZOMBIE;
    }
    return PRS_EXITED;
  }

  // if the process has no parent it also enters the PRS_EXITED state
  return PRS_EXITED;
}

static int proc_child_notify_parent(proc_t *proc, pid_t pid, int status) {
  pr_lock_assert(proc, LA_OWNED);
  proc_t *parent = proc->parent;
  if (parent == NULL) {
    return 0;
  }

  // if the process is exited no need to notify the parent
  if (proc->state == PRS_EXITED) {
    return 0;
  }

  // send a child status event to the parent
  struct pchild_status event = {pid, status};
  if (chan_send(parent->wait_status_ch, &event) < 0) {
    EPRINTF("failed to send child wait status to parent proc {:pr}\n", parent);
    return -EPIPE;
  }
  return 0;
}

static inline bool sig_has_coredump(int sig) {
  switch (sig) {
    // signals that cause a core dump
    case SIGQUIT: case SIGILL: case SIGABRT: case SIGFPE: case SIGSEGV:
    case SIGBUS: case SIGSYS: case SIGTRAP: case SIGXCPU: case SIGXFSZ:
      return true;
    // signals that do not cause a core dump
    default:
      return false;
  }
}

// proc api

pid_t proc_alloc_pid() {
  pid_t pid = pidset_alloc();
  ASSERT(pid != -1);
  return pid;
}

void proc_free_pid(pid_t pid) {
  pidset_free(pid);
}

__ref proc_t *proc_alloc_internal(
  pid_t pid,
  struct address_space *space,
  struct pcreds *creds,
  struct ventry *pwd,
  bool fork
) {
  proc_t *proc = kmallocz(sizeof(proc_t));
  proc->pid = pid;
  proc->space = space;
  proc->creds = getref(creds);
  proc->pwd = fs_root_getref();
  proc->state = PRS_EMPTY;

  if (!fork) {
    // during fork these fields are derived from the parent
    proc->files = ftable_alloc();
    proc->usage = kmallocz(sizeof(struct rusage));
    proc->limit = kmallocz(sizeof(struct rlimit));
    proc->stats = kmallocz(sizeof(struct pstats));
    proc->sigacts = sigacts_alloc();
  }

  ref_init(&proc->refcount);
  sigqueue_init(&proc->sigqueue);

  mtx_init(&proc->lock, MTX_RECURSIVE, "proc_lock");
  mtx_init(&proc->statlock, MTX_SPIN, "proc_statlock");
  proc->wait_status_ch = chan_alloc(64, sizeof(struct pchild_status), CHAN_NOBLOCK, "proc_child_sts_ch");
  cond_init(&proc->signal_cond, "proc_signal_cond");
  cond_init(&proc->td_exit_cond, "proc_td_exit_cond");

  if (!fork) {
    // on fork the process joins the parent's process group
    pgroup_t *pgrp = pgrp_alloc_add_proc(proc);
    pgrp_putref(&pgrp);
  }
  return proc; __ref
}

__ref proc_t *proc_alloc_new(struct pcreds *creds) {
  return proc_alloc_internal(
    proc_alloc_pid(),
    vm_new_empty_space(),
    creds ? creds : pcreds_alloc(0, 0),
    fs_root_getref(),
    /*fork=*/false
  );
}

__ref proc_t *proc_fork() {
  proc_t *proc = curproc;
  pr_lock(proc);
  address_space_t *space = proc->space;

  mtx_lock(&space->lock);
  address_space_t *new_space = vm_fork_space(space, /*fork_user=*/true);
  mtx_unlock(&space->lock);

  proc_t *new_proc = proc_alloc_internal(
    proc_alloc_pid(),
    new_space,
    getref(proc->creds),
    ve_getref(proc->pwd),
    /*fork=*/true
  );
  ASSERT(new_proc != NULL);

  new_proc->files = ftable_clone(proc->files);
  new_proc->usage = kmalloc_cp(proc->usage, sizeof(struct rusage));
  new_proc->limit = kmalloc_cp(proc->limit, sizeof(struct rlimit));
  new_proc->stats = kmalloc_cp(proc->stats, sizeof(struct pstats));
  new_proc->sigacts = sigacts_clone(proc->sigacts);

  pgrp_lock(proc->group);
  pgrp_add_proc(proc->group, new_proc);
  pgrp_unlock(proc->group);

  new_proc->args = pstrings_copy(proc->args);
  new_proc->env = pstrings_copy(proc->env);
  new_proc->binpath = str_dup(proc->binpath);

  new_proc->brk_start = proc->brk_start;
  new_proc->brk_end = proc->brk_end;
  new_proc->brk_max = proc->brk_max;

  new_proc->name = str_dup(proc->name);
  new_proc->parent = pr_getref(proc);
  LIST_ADD(&proc->children, pr_getref(new_proc), chldlist);

  pr_unlock(proc);
  return new_proc; __ref
}

void _proc_cleanup(__move proc_t **procp) {
  proc_t *proc = moveref(*procp);
  pr_lock_assert(proc, LA_LOCKED);
  ASSERT(PRS_IS_EXITED(proc));
  if (mtx_owner(&proc->lock) != NULL) {
    ASSERT(mtx_owner(&proc->lock) == curthread);
    mtx_unlock(&proc->lock);
  }

  proc_free_pid(proc->pid);
  pcreds_release(&proc->creds);
  ve_putref(&proc->pwd);

  ftable_free(&proc->files);
  sigacts_free(&proc->sigacts);

  kfreep(&proc->usage);
  kfreep(&proc->limit);
  kfreep(&proc->stats);

  mtx_destroy(&proc->lock);
  mtx_destroy(&proc->statlock);
  chan_free(proc->wait_status_ch);
  cond_destroy(&proc->signal_cond);
  cond_destroy(&proc->td_exit_cond);

  kfree(proc);
}

void proc_setup_add_thread(proc_t *proc, thread_t *td) {
  ASSERT(PRS_IS_EMPTY(proc));
  ASSERT(TDS_IS_EMPTY(td))
  ASSERT(td->proc == NULL);
  proc_do_add_thread(proc, td);
}

int proc_setup_exec_args(proc_t *proc, const char *const args[]) {
  ASSERT(PRS_IS_EMPTY(proc));
  ASSERT(proc->args == NULL);
  return pstrings_alloc(args, ARG_MAX, &proc->args);
}

int proc_setup_exec_env(proc_t *proc, const char *const env[]) {
  ASSERT(PRS_IS_EMPTY(proc));
  ASSERT(proc->env == NULL);
  return pstrings_alloc(env, ENV_MAX, &proc->env);
}

int proc_setup_exec(proc_t *proc, cstr_t path) {
  ASSERT(PRS_IS_EMPTY(proc));
  ASSERT(pr_main_thread(proc) != NULL);
  ASSERT(pr_main_thread(proc)->tcb->rip == 0);
  if (proc->args == NULL) {
    proc->args = kmallocz(sizeof(struct pstrings));
  }
  if (proc->env == NULL) {
    proc->env = kmallocz(sizeof(struct pstrings));
  }

  struct exec_image *image = NULL;
  struct exec_stack *stack = NULL;
  int res;

  // load the executable and prepare the stack
  if ((res = exec_load_image(EXEC_BIN, 0, path, &image)) < 0)
    return res;
  if ((res = exec_image_setup_stack(image, PROC_USTACK_BASE, PROC_USTACK_SIZE, proc->creds, proc->args, proc->env, &stack)) < 0)
    goto ret;

  // place the process `brk` mapping after the last data segment
  vm_desc_t *last_segment = SLIST_GET_LAST(image->descs, next);
  uintptr_t last_segment_end = last_segment->address + last_segment->size;

  // map the image(s) and stack descriptors into the new process space
  if (vm_desc_map_space(proc->space, image->descs)< 0) {
    EPRINTF("failed to map image descriptors\n");
    goto_res(ret, -ENOMEM);
  }
  if (image->interp && vm_desc_map_space(proc->space, image->interp->descs) < 0) {
    EPRINTF("failed to map interpreter descriptors\n");
    goto_res(ret, -ENOMEM);
  }
  if (vm_desc_map_space(proc->space, stack->descs) < 0) {
    EPRINTF("failed to map stack descriptors\n");
    goto_res(ret, -ENOMEM);
  }

  // create a mapping for the process `brk` segment that reserves the virtual space
  // but initially has no size. the segment will be expanded as needed by the process
  if (vmap_other_anon(proc->space, PROC_BRK_MAX, last_segment_end, 0, VM_RDWR|VM_FIXED, "brk") == 0) {
    EPRINTF("failed to map brk segment\n");
    goto_res(ret, -ENOMEM);
  }

  proc->binpath = str_move(image->path);
  proc->brk_start = last_segment_end;
  proc->brk_end = last_segment_end;
  proc->brk_max = last_segment_end + PROC_BRK_MAX;

  thread_t *td = pr_main_thread(proc);
  td->tcb->rip = image->interp ? image->interp->entry : image->entry;
  td->tcb->rsp = stack->base + stack->off;
  td->tcb->rflags = 0x202; // IF=1, IOPL=0
  if (TDF_IS_KTHREAD(td))
    td->tcb->rflags |= 0x3000; // IF=1, IOPL=3
  else
    td->tcb->tcb_flags |= TCB_SYSRET;

  td->name = str_dup(proc->binpath);
  td->ustack_base = stack->base;
  td->ustack_size = stack->size;

  res = 0; // success
LABEL(ret);
  exec_free_image(&image);
  exec_free_stack(&stack);
  return res;
}

int proc_setup_entry(proc_t *proc, uintptr_t function, int argc, ...) {
  ASSERT(PRS_IS_EMPTY(proc));
  ASSERT(pr_main_thread(proc) != NULL);
  ASSERT(proc->args == NULL);
  ASSERT(proc->env == NULL);

  thread_t *td = pr_main_thread(proc);

  va_list args;
  va_start(args, argc);
  thread_setup_entry_va(td, function, argc, args);
  va_end(args);
  return 0;
}

int proc_setup_open_fd(proc_t *proc, int fd, cstr_t path, int flags) {
  ASSERT(PRS_IS_EMPTY(proc));
  ASSERT(!(flags & O_CREAT));
  int res;
  if ((res = fs_proc_open(proc, fd, path, flags, 0))) {
    return res;
  }
  return res;
}

int proc_setup_name(proc_t *proc, cstr_t name) {
  ASSERT(PRS_IS_EMPTY(proc));
  if (!str_isnull(proc->name)) {
    str_free(&proc->name);
  }

  proc->name = str_from_cstr(name);

  thread_t *maintd = pr_main_thread(proc);
  if (maintd != NULL && str_isnull(maintd->name)) {
    maintd->name = str_dup(proc->name);
  }
  return 0;
}

void proc_finish_setup_and_submit_all(proc_t *proc) {
  ASSERT(PRS_IS_EMPTY(proc));
  ASSERT(proc->num_threads > 0);

  proc->state = PRS_ACTIVE;
  LIST_FOR_IN(td, &proc->threads, plist) {
    thread_finish_setup_and_submit(td);
  }

  // insert into process table
  ptable_add_proc(proc->pid, proc);
}

//

__ref proc_t *proc_lookup(pid_t pid) {
  proc_t *proc = ptable_get_proc(pid);
  if (proc == NULL) {
    return NULL;
  }
  return proc; __ref
}

bool proc_is_pgrp_leader(proc_t *proc) {
  pr_lock_assert(proc, MA_OWNED);
  pid_t pgid = proc->group->pgid;
  return proc->pid == pgid;
}

bool proc_is_sess_leader(proc_t *proc) {
  pr_lock_assert(proc, MA_OWNED);
  pid_t sid = proc->group->session->sid;
  return proc->pid == sid;
}

void proc_add_thread(proc_t *proc, thread_t *td) {
  ASSERT(PRS_IS_ALIVE(proc));
  ASSERT(TDS_IS_EMPTY(td));
  ASSERT(td->proc == NULL);

  pr_lock(proc);
  if (PRS_IS_DEAD(proc)) {
    EPRINTF("called on dead process %d\n", proc->pid);
    pr_unlock(proc);
    return;
  }

  td_lock(td);
  proc_do_add_thread(proc, td);
  sched_submit_new_thread(td);
  td_unlock(td);
  pr_unlock(proc);
}

void proc_terminate(proc_t *proc, int ret, int sig) {
  pr_lock(proc);
  if (PRS_IS_DEAD(proc)) {
    EPRINTF("called on dead process %d\n", proc->pid);
    pr_unlock(proc);
    return;
  }

  // compute the exit status
  int status = W_EXITCODE(ret, sig);
  status |= sig_has_coredump(sig) ? W_COREDUMP : 0; // set the core dump bit if applicable

  DPRINTF("proc %d terminating with status %d\n", proc->pid, ret);
  proc->state = proc_get_exit_state(proc);
  proc->exit_status = status;

  if (proc->pending_alarm > 0) {
    // cancel pending signal alarm
    alarm_unregister(proc->pending_alarm);
    proc->pending_alarm = 0;
  }

  // notify parent process of the termination
  proc_child_notify_parent(proc, proc->pid, status);

  // terminate process threads
  LIST_FOR_IN(td, &proc->threads, plist) {
    // skip if it's the current thread, it will be killed
    // at the end of this function when it reschedules
    if (td != curthread) {
      thread_kill(td);
    }
  }

  if (proc == curproc) {
    // exit the current thread, this will release
    // the process lock and reference and reschedule
    thread_kill(curthread);
    unreachable;
  }

  pr_unlock(proc);
}

void proc_kill_tid(proc_t *proc, pid_t tid, int ret, int sig) {
  pr_lock(proc);
  if (PRS_IS_DEAD(proc)) {
    EPRINTF("called on dead process %d\n", proc->pid);
    pr_unlock(proc);
    return;
  }

  thread_t *td = LIST_FIND(_td, &proc->threads, plist, tid == _td->tid);
  if (td == NULL) {
    EPRINTF("thread %d not found in process %d\n", tid, proc->pid);
    pr_unlock(proc);
    return;
  }

  if (proc->num_threads == 1 || td->tid == 0) {
    // killing the last or main thread is equivalent to
    // calling proc_kill to terminate the process
    pr_unlock(proc);
    proc_terminate(proc, ret, sig);
    return;
  }

  DPRINTF("proc {:pr} killing thread %d\n", proc, tid);
  if (td == curthread) {
    thread_kill(curthread);
    unreachable;
  } else {
    thread_kill(td);
    pr_unlock(proc);
  }
}

void proc_stop(proc_t *proc, int sig) {
  pr_lock(proc);
  if (PRS_IS_DEAD(proc)) {
    EPRINTF("called on dead process {:pr}\n", proc);
    pr_unlock(proc);
    return;
  }

  DPRINTF("proc {:pr} stopping\n", proc);
  atomic_fetch_or(&proc->flags, PRF_STOPPED);

  // notify parent process of the stop
  proc_child_notify_parent(proc, proc->pid, W_STOPCODE(sig));

  if (proc == curproc) {
    // stop other threads first
    LIST_FOR_IN(td, &proc->threads, plist) {
      if (td != curthread) {
        thread_stop(td);
      }
    }
    pr_unlock(proc);
    // stop the current thread
    thread_stop(curthread);
    return; // returns when thread is resumed
  }

  // stop all the threads
  LIST_FOR_IN(td, &proc->threads, plist) {
    thread_stop(td);
  }
  pr_unlock(proc);
}

void proc_cont(proc_t *proc) {
  ASSERT(proc != curproc);
  pr_lock(proc);
  if (PRS_IS_DEAD(proc)) {
    EPRINTF("called on dead process {:pr}\n", proc);
    pr_unlock(proc);
    return;
  }

  DPRINTF("proc {:pr} continuing\n", proc);
  atomic_fetch_and(&proc->flags, ~PRF_STOPPED);

  // notify parent process of the stop
  proc_child_notify_parent(proc, proc->pid, W_CONTINUED);

  // start all the threads
  LIST_FOR_IN(td, &proc->threads, plist) {
    thread_cont(td);
  }
  pr_unlock(proc);
}

int proc_wait_signal(proc_t *proc) {
  pr_lock(proc);
  cond_wait(&proc->signal_cond, &proc->lock);
  // proc is locked
  pr_unlock(proc);
  return 0;
}

int proc_signal(proc_t *proc, int sig, int si_code, union sigval si_value) {
  if (sig < 0 || sig >= NSIG) {
    return -EINVAL;
  }

  int res;
  bool locked = false;
  if (mtx_owner(&proc->lock) != curthread) {
    pr_lock(proc);
    locked = true;
  }

  if (!PRS_IS_ALIVE(proc)) {
    EPRINTF("called on dead process {:pr}\n", proc);
    goto_res(ret, -ESRCH);
  }

  struct sigaction sa;
  enum sigdisp disp;
  if ((res = sigacts_get(proc->sigacts, sig, &sa, &disp)) < 0) {
    goto ret;
  }

  if (disp == SIGDISP_IGN) {
    EPRINTF("signal %d ignored by proc {:pr}\n", sig, proc);
    goto_res(ret, 0);
  }

  if (disp == SIGDISP_TERM) {
    proc_terminate(proc, 0, sig);
    goto_res(ret, 0);
  } else if (disp == SIGDISP_STOP) {
    proc_stop(proc, sig);
    goto_res(ret, 0);
  } else if (disp == SIGDISP_CONT) {
    proc_cont(proc);
    goto_res(ret, 0);
  } else if (disp == SIGDISP_CORE) {
    DPRINTF("core dump requested for proc {:pr} with signal %d\n", proc, sig);
    DPRINTF("====== core dump not implemented ======\n");
    proc_terminate(proc, 0, sig);
    goto_res(ret, 0);
  }

  // the signal has a handler installed
  // select a thread to run the handler
  thread_t *td = NULL;
  LIST_FOR_IN(_td, &proc->threads, plist) {
    if (!sigset_masked(_td->sigmask, sig)) {
      td = _td;
      break;
    }
  }

  if (td == NULL) {
    goto_res(ret, -ESRCH);
  }

  // send the signal to the thread
  if ((res = thread_signal(td, sig, si_code, si_value)) < 0) {
    goto ret;
  }
  cond_broadcast(&proc->signal_cond);

  res = 0; // success
LABEL(ret);
  if (locked)
    pr_unlock(proc);
  return res;
}

int pid_signal(pid_t pid, int sig, int si_code, union sigval si_value) {
  proc_t *proc = ptable_get_proc(pid); __ref
  if (proc == NULL) {
    return -ESRCH;
  }

  int res = proc_signal(proc, sig, si_code, si_value);
  pr_putref(&proc);
  return res;
}

int proc_syscall_wait4(pid_t pid, int *status, int options, struct rusage *rusage) {
  proc_t *proc = curproc;
  pid_t cur_pid = proc->pid;
  pid_t curr_pgid = proc->group->session->sid;;

  int res;
  pid_t child_pid;
  pid_t child_pgid;
  proc_t *child = NULL;

  struct pchild_status event;
  int rx_opts = (options & WNOHANG) ? CHAN_RX_NOBLOCK : 0;
  while ((res = chan_recv_opts(proc->wait_status_ch, &event, rx_opts)) >= 0) {
    ASSERT(event.pid != 0 && event.pid != cur_pid);
    child_pid = event.pid;
    int wstatus = event.status;

    child = proc_lookup(child_pid);
    if (child == NULL) {
      EPRINTF("wait4: child process %d not found\n", child_pid);
      continue;
    }

    pr_lock(child);
    if (child->state == PRS_EXITED) {
      // child already exited
      goto next_child;
    }

    child_pgid = child->group->pgid;

    // check if the child process matches the wait criteria
    bool matching = false;
    if (pid < -1) { // wait for any child process whose pgid == -pid
      matching = (-pid == child_pgid);
    } else if (pid == -1) { // wait for any child process
      matching = true;
    } else if (pid == 0) { // wait for any child process whose pgid == curproc's pgid
      matching = (child_pgid == curr_pgid);
    } else if (pid > 0) { // wait for the child whose pid == pid
      matching = (child_pid == pid);
    }

    if (!matching) {
      // child does not match the wait criteria
      goto next_child;
    }

    // check if the state change matches the wait options
    if ((WIFSTOPPED(wstatus) && !(options & WUNTRACED))) {
      // if WUNTRACED is not set, we do not want to wait for stopped children
      goto next_child;
    }

    // we have a matching child process
    break;

    // ignore this child process
  LABEL(next_child);
    pr_unlock(child);
    pr_putref(&child);
  }

  if (res < 0) {
    if (res == -EAGAIN) {
      // WNOHANG was set and no child processes are available
      if (status != NULL) {
        *status = 0; // no child processes, set status to 0
      }
      return 0; // no child processes to wait for
    }

    // wait_status channel is closed, no more child processes to wait for
    EPRINTF("wait4: child wait status channel closed\n");
    return res; // -EPIPE
  }

  // we can now reap the child process
  ASSERT(child->parent == proc);
  child->state = PRS_EXITED;
  proc_t *parent = child->parent;
  pr_lock(parent);
  LIST_REMOVE(&parent->children, child, chldlist);
  pr_unlock(parent);
  pr_unlock(child);
  ptable_remove_proc(child_pid, moveref(child));
  return child_pid;
}

int proc_syscall_execve(cstr_t path, char *const argv[], char *const envp[]) {
  proc_t *proc = curproc;
  thread_t *td = curthread;
  struct exec_image *image = NULL;
  struct exec_stack *stack = NULL;
  struct pstrings *args;
  struct pstrings *env;
  int res;

  // load the executable
  if ((res = exec_load_image(EXEC_BIN, 0, path, &image)) < 0)
    return res;

  // copy the arg and env strings into kernel memory
  if ((res = pstrings_alloc(argv, ARG_MAX, &args)) < 0) {
    goto fail;
  }
  if ((res = pstrings_alloc(envp, -1, &env)) < 0) {
    pstrings_free(&args);
    goto fail;
  }

  // prepare the stack for the new image
  if ((res = exec_image_setup_stack(image, PROC_USTACK_BASE, PROC_USTACK_SIZE, proc->creds, args, env, &stack)) < 0) {
    pstrings_free(&args);
    pstrings_free(&env);
    goto fail;
  }

  // tear down the old process state
  pr_lock(proc);
  td_lock(td);

  // 1. stop all other threads and remove them
  thread_t *other = LIST_FIRST(&proc->threads);
  while (other != NULL) {
    thread_t *next = LIST_NEXT(other, plist);
    if (other == td) {
      other = next; // skip the current thread
      continue;
    }

    thread_kill(other);
    thread_free_exited(&other);
    other = next;
  }

  // 2. replace existing arg and env strings with the new ones
  pstrings_free(&proc->args);
  pstrings_free(&proc->env);
  proc->args = args;
  proc->env = env;

  // 3. close any open directory streams and files opened with O_CLOEXEC
  ftable_exec_close(proc->files);

  // 4. reset all signal handlers to default
  sigacts_reset(proc->sigacts);

  // 5. cancel process pending alarms
  if (proc->pending_alarm > 0) {
    alarm_unregister(proc->pending_alarm);
    proc->pending_alarm = 0;
  }

  // 6. clear all existing user vm mappings
  vm_clear_user_space(curspace);

  // TODO: handle set_tid_address, clear_child_tid
  // TODO: reset floating point environment

  // THIS IS THE POINT OF NO RETURN
  // any error after this point will result in a process crash

  // map the image(s) and stack descriptors into the process space
  if (vm_desc_map_space(curspace, image->descs)< 0) {
    EPRINTF("failed to map image descriptors\n");
    goto_res(crash, -ENOMEM);
  }
  if (image->interp && vm_desc_map_space(curspace, image->interp->descs) < 0) {
    EPRINTF("failed to map interpreter descriptors\n");
    goto_res(crash, -ENOMEM);
  }
  if (vm_desc_map_space(curspace, stack->descs) < 0) {
    EPRINTF("failed to map stack descriptors\n");
    goto_res(crash, -ENOMEM);
  }

  // create the `brk` segment after the last data segment
  vm_desc_t *last_segment = SLIST_GET_LAST(image->descs, next);
  uintptr_t last_segment_end = last_segment->address + last_segment->size;
  if (vmap_anon(PROC_BRK_MAX, last_segment_end, 0, VM_USER|VM_RDWR|VM_FIXED, "brk") == 0) {
    EPRINTF("failed to map brk segment\n");
    goto_res(crash, -ENOMEM);
  }

  if (!str_isnull(proc->binpath))
    str_free(&proc->binpath);
  proc->binpath = str_move(image->path);
  proc->brk_start = last_segment_end;
  proc->brk_end = last_segment_end;
  proc->brk_max = last_segment_end + PROC_BRK_MAX;

  td->tid = 0; // the calling thread becomes the main thread
  td->ustack_base = stack->base;
  td->ustack_size = stack->size;
  if (!str_isnull(td->name))
    str_free(&td->name);
  td->name = str_dup(proc->binpath);

  // reset the threads trapframe and clear it
  uintptr_t kstack_top = td->kstack_base + td->kstack_size;
  kstack_top -= align(sizeof(struct tcb), 16);
  td->tcb = (struct tcb *) kstack_top;
  memset((void *) td->tcb, 0, sizeof(struct tcb));
  kstack_top -= sizeof(struct trapframe);
  td->frame = (struct trapframe *) kstack_top;
  memset((void *) td->frame, 0, sizeof(struct trapframe));

  DPRINTF("execve: thread->frame = {:p}, thread->tcb = {:p}\n", td->frame, td->tcb);

  td_unlock(td);
  pr_unlock(proc);

  uintptr_t rip = image->interp ? image->interp->entry : image->entry;
  uintptr_t rsp = stack->base + stack->off;
  uint32_t rflags = 0x3202; // IF=1, IOPL=3
  ASSERT(is_aligned(rsp, 16));

  // start running the new image
  exec_free_image(&image);
  exec_free_stack(&stack);
  sysret(rip, rsp, rflags);
  unreachable;

LABEL(fail);
  exec_free_image(&image);
  exec_free_stack(&stack);
  return res;

LABEL(crash);
  td_unlock(td);
  pr_unlock(proc);
  exec_free_image(&image);
  exec_free_stack(&stack);
  EPRINTF("execve failed, crashing process {:pr}: {:err}\n", proc, res);
  proc_terminate(proc, 0, SIGKILL); // forcefully terminate the process
  unreachable;
}

///////////////////
// MARK: thread

static inline uint8_t td_flags_to_base_priority(uint32_t td_flags) {
  if (td_flags & TDF_ITHREAD) {
    return PRI_REALTIME;
  } else if (td_flags & TDF_IDLE) {
    return PRI_IDLE;
  } else {
    return PRI_NORMAL;
  }
}

// thread api

static thread_t *thread_alloc_internal(uint32_t flags, uintptr_t kstack_base, size_t kstack_size) {
  thread_t *td = kmallocz(sizeof(thread_t));
  td->flags = flags;
  td->flags2 = TDF2_FIRSTTIME;

  td->cpuset = cpuset_alloc(NULL);
  td->own_lockq = lockq_alloc();
  td->own_waitq = waitq_alloc();
  td->wait_claims = lock_claim_list_alloc();

  td->state = TDS_EMPTY;
  td->cpu_id = -1;
  td->pri_base = td_flags_to_base_priority(flags);
  td->priority = td->pri_base;
  mtx_init(&td->lock, MTX_SPIN, "thread_lock");

  td->kstack_base = kstack_base;
  td->kstack_size = kstack_size;

  // kstack top   --------------
  //        tcb ->
  //  trapframe ->
  //                   ...
  // kstack base  --------------
  uintptr_t kstack_top = td->kstack_base + td->kstack_size;
  kstack_top -= align(sizeof(struct tcb), 16);
  td->tcb = (struct tcb *) kstack_top;
  memset((void *) td->tcb, 0, sizeof(struct tcb));
  kstack_top -= sizeof(struct trapframe);
  td->frame = (struct trapframe *) kstack_top;
  memset((void *) td->frame, 0, sizeof(struct trapframe));

  if (flags & TDF_KTHREAD) {
    td->tcb->tcb_flags |= TCB_KERNEL;
  } else {
    td->tcb->tcb_flags |= TCB_SYSRET;
  }

  sigqueue_init(&td->sigqueue);
  return td;
}

thread_t *thread_alloc(uint32_t flags, size_t kstack_size) {
  ASSERT(kstack_size > 0 && is_aligned(kstack_size, PAGE_SIZE));
  uintptr_t kstack_base = vmap_pages(alloc_pages(SIZE_TO_PAGES(kstack_size)), 0, kstack_size, VM_RDWR|VM_STACK, "kstack");
  return thread_alloc_internal(flags, kstack_base, kstack_size);
}

thread_t *thread_alloc_proc0_main() {
  uintptr_t curr_stack_base = (uintptr_t)&entry_initial_stack;
  uintptr_t curr_stack_top = (uintptr_t)&entry_initial_stack_top;
  return thread_alloc_internal(TDF_KTHREAD|TDF_NOPREEMPT, curr_stack_base, curr_stack_top - curr_stack_base);
}

thread_t *thread_alloc_idle() {
  // this is called once by each cpu during scheduler initialization
  thread_t *td = thread_alloc(TDF_KTHREAD|TDF_IDLE|TDF_NOPREEMPT, SIZE_8KB);
  td->name = str_fmt("idle thread [CPU#%d]", curcpu_id);

  thread_setup_entry(td, (uintptr_t) idle_thread_entry, 0);

  mtx_spin_lock(&proc0_ap_lock);
  proc_do_add_thread(proc0, td);
  mtx_spin_unlock(&proc0_ap_lock);
  return td;
}

thread_t *thread_syscall_fork() {
  // MUST BE CALLED FROM WITHIN A SYSCALL
  thread_t *td = curthread;
  ASSERT(!TDS_IS_EXITED(td));
  td_lock(td);
  thread_t *copy = thread_alloc(td->flags, td->kstack_size);

  // copy the kernel stack
  memcpy((void *)copy->kstack_base, (void *)td->kstack_base, td->kstack_size);
  // copy the syscall trapframe into the thread
  memcpy((void *)copy->frame, (void *)td->frame, sizeof(struct trapframe));
  // in the current thread frame->parent points to original on-stack trapframe
  // since it was replaced by the temporary syscall trapframe. we dont want to
  // propagate this to the forked thread since we are already using the main frame.
  copy->frame->parent = NULL;
  copy->frame->flags = TF_SYSRET;
  // we set the syscall return value for the forked process here
  copy->frame->rax = 0; // must return 0

  // we want this thread to be restored from the trapframe
  copy->flags2 |= TDF2_TRAPFRAME;
  copy->name = str_dup(td->name);

  DPRINTF("fork: thread->frame = {:p}, thread->tcb = {:p}\n", copy->frame, copy->tcb);

  td_unlock(td);
  return copy;
}

void thread_free_exited(thread_t **tdp) {
  thread_t *td = *tdp;
  ASSERT(TDS_IS_EXITED(td));
  if (mtx_owner(&td->lock) != NULL) {
    ASSERT(mtx_owner(&td->lock) == curthread);
    td_unlock(td);
  }

  pr_putref(&td->proc);
  pcreds_release(&td->creds);
  lock_claim_list_free(&td->wait_claims);
  lockq_free(&td->own_lockq);
  waitq_free(&td->own_waitq);
  cpuset_free(&td->cpuset);

  mtx_destroy(&td->lock);
  kfree(td);
  *tdp = NULL;
}

int thread_setup_entry(thread_t *td, uintptr_t function, int argc, ...) {
  va_list arglist;
  va_start(arglist, argc);
  int res = thread_setup_entry_va(td, function, argc, arglist);
  va_end(arglist);
  return res;
}

int thread_setup_entry_va(thread_t *td, uintptr_t function, int argc, va_list arglist) {
  ASSERT(TDS_IS_EMPTY(td));
  ASSERT(td->tcb->rip == 0);
  ASSERT(TDF_IS_KTHREAD(td));

  if (argc > 6) {
    EPRINTF("too many arguments\n");
    return -EINVAL;
  }

  void *args[6] = {0};
  for (int i = 0; i < argc; i++) {
    args[i] = va_arg(arglist, void *);
  }

  if (str_isnull(td->name)) {
    td->name = str_from_charp(debug_function_name(function));
  }

  if (!is_kernel_code_ptr(function)) {
    EPRINTF("warning: kernel thread entry point is not a kernel code pointer\n");
  }

  td->tcb->tcb_flags |= TCB_KERNEL;
  td->tcb->rflags = 0x202; // IF=1, IOPL=0
  td->tcb->rip = (uintptr_t) kernel_thread_entry;

  uintptr_t rsp = thread_get_kstack_top(td);
  rsp -= sizeof(void *) * 7; // function + 6 args
  ((void **) rsp)[0] = (void *) function;
  for (int i = 0; i < 6; i++) {
    ((void **) rsp)[i + 1] = args[i];
  }

  td->tcb->rsp = rsp;
  return 0;
}

int thread_setup_name(thread_t *td, cstr_t name) {
  ASSERT(TDS_IS_EMPTY(td));
  if (!str_isnull(td->name)) {
    str_free(&td->name);
  }

  td->name = str_from_cstr(name);
  return 0;
}

void thread_setup_priority(thread_t *td, uint8_t base_pri) {
  ASSERT(TDS_IS_EMPTY(td));
  td->pri_base = base_pri;
  td->priority = base_pri;
}

void thread_finish_setup_and_submit(thread_t *td) {
  ASSERT(TDS_IS_EMPTY(td));
  ASSERT(td->proc != NULL);

  td_lock(td);
  sched_submit_new_thread(td);
  ASSERT(TDS_IS_READY(td));
  // td is now owned by the runq
  td_unlock(td);
}

//

void thread_kill(thread_t *td) {
  proc_t *proc = td->proc;
  pr_lock_assert(proc, LA_OWNED);
  td_lock_assert(td, LA_NOTOWNED);

  // mark the thread stopped now so it doesn't get scheduled
  atomic_fetch_or(&td->flags2, TDF2_STOPPED);

  td_lock(td);
  ASSERT(!TDS_IS_EXITED(td));

  // remove the thread from the process thread list
  proc_do_remove_thread(proc, td);
  if (TDS_IS_RUNNING(td)) {
    // stop an active thread
    if (td == curthread) {
      // the current thread is exiting so we must force the release
      // of the process lock and reference held by the caller since
      // this function will not return
      pr_lock_assert(proc, LA_OWNED);
      pr_lock_assert(proc, LA_NOTRECURSED);
      pr_unlock(proc);
      pr_putref(&proc);
      // thread cleanup is deferred to the scheduler
      sched_again(SCHED_EXITED);
      unreachable;
    } else {
      // stop thread running on another cpu
      uint32_t num_exited = atomic_load(&proc->num_exited);
      sched_cpu(td->cpu_id, SCHED_EXITED);
      td_unlock(td);

      // wait for the thread to exit
      while (atomic_load(&proc->num_exited) == num_exited) {
        cond_wait(&proc->td_exit_cond, &proc->lock);
      }
      // thread freeing is deferred to the scheduler of the cpu
    }
  } else {
    // thread isnt currently active
    if (TDS_IS_READY(td)) {
      // remove ready thread from the scheduler
      sched_remove_ready_thread(td);
    } else if (TDS_IS_BLOCKED(td)) {
      // remove blocked thread from lockqueue
      struct lockqueue *lq = lockq_lookup(td->contested_lock);
      lockq_remove(lq, td, td->lockq_num);
      td->contested_lock = NULL;
      td->lockq_num = -1;
    } else if (TDS_IS_WAITING(td)) {
      // remove thread from the associated waitqueue
      struct waitqueue *wq = waitq_lookup(td->wchan);
      if (wq->type == WQ_SLEEP) {
        // cancel the pending alarm
        alarm_t *alarm = td->wchan;
        alarm_unregister(alarm->id);
      }
      waitq_remove(wq, td);
      td->wchan = NULL;
      td->wdmsg = NULL;
    }

    TD_SET_STATE(td, TDS_EXITED);
    atomic_fetch_add(&proc->num_exited, 1);
    cond_broadcast(&proc->td_exit_cond);

    // there should be no more references to the thread
    // so we can free it now
    thread_free_exited(&td);
    return;
  }
}

void thread_stop(thread_t *td) {
  td_lock_assert(td, MA_NOTOWNED);

  td_lock(td);
  if (TDS_IS_EXITED(td)) {
    EPRINTF("thread already exited {:td}\n", td);
    goto done;
  } else if (TDF2_IS_STOPPED(td)) {
    EPRINTF("thread already stopped {:td}\n", td);
    goto done;
  }

  atomic_fetch_or(&td->flags2, TDF2_STOPPED);
  if (TDS_IS_RUNNING(td)) {
    if (td == curthread) {
      // stop the current thread
      sched_again(SCHED_YIELDED);
      return; // returns on continue
    }

    // stop thread running on another cpu
    sched_cpu(td->cpu_id, SCHED_YIELDED);
    goto done;
  }

  if (TDS_IS_READY(td)) {
    // remove ready thread from the scheduler
    sched_remove_ready_thread(td);
  }

  // for TDS_BLOCKED and TDS_WAITING, the thread remains in the respecive
  // lockqueue or waitqueue but since its marked as TDF2_STOPPED, it will
  // not be signaled

LABEL(done);
  td_unlock(td);
}

void thread_cont(thread_t *td) {
  td_lock_assert(td, MA_NOTOWNED);

  td_lock(td);
  if (TDS_IS_EXITED(td)) {
    EPRINTF("thread has exited {:td}\n", td);
    goto done;
  } else if (!TDF2_IS_STOPPED(td)) {
    EPRINTF("thread is not stopped {:td}\n", td);
    goto done;
  }

  atomic_fetch_and(&td->flags2, ~TDF2_STOPPED);
  if (TDS_IS_READY(td)) {
    // reinsert the thread into the scheduler
    sched_submit_ready_thread(td);
  }

  // for TDS_BLOCKED and TDS_WAITING, the thread can now be signaled with
  // the TDF2_STOPPED flag cleared.

LABEL(done);
  td_unlock(td);
}

int thread_signal(thread_t *td, int sig, int si_code, union sigval si_value) {
  if (sig < 0 || sig >= NSIG) {
    return -EINVAL;
  }

  td_lock(td);
  // signal may be masked but we put it into the queue anyways
  // to allow the signal to be delivered when it is unmasked
  sigqueue_push(&td->sigqueue, &(struct siginfo){
    .si_signo = sig,
    .si_code = si_code,
    .si_value = si_value,
  });
  td->flags2 |= TDF2_SIGPEND;
  td_unlock(td);
  return 0;
}

///////////////////
// MARK: cpuset

#define CPUSET_MAX_INDEX (MAX_CPUS / 64)
#define cpuset_index(cpu) ((cpu) / 64)
#define cpuset_offset(cpu) ((cpu) % 64)
#define cpuset_cpu(index, offset) (((index) * 64) + (offset))

struct cpuset {
  uint64_t bits[MAX_CPUS / 64];
  size_t ncpus;
};

struct cpuset *cpuset_alloc(struct cpuset *existing) {
  if (existing) {
    struct cpuset *copy = kmallocz(sizeof(struct cpuset));
    memcpy(copy->bits, existing->bits, sizeof(copy->bits));
    copy->ncpus = existing->ncpus;
    return copy;
  } else {
    return kmallocz(sizeof(struct cpuset));
  }
}

void cpuset_free(struct cpuset **set) {
  kfree(*set);
  *set = NULL;
}

void cpuset_set(struct cpuset *set, int cpu) {
  ASSERT(cpu < MAX_CPUS);
  set->bits[cpuset_index(cpu)] |= (1ULL << cpuset_offset(cpu));
  set->ncpus++;
}

void cpuset_reset(struct cpuset *set, int cpu) {
  ASSERT(cpu < MAX_CPUS);
  set->bits[cpuset_index(cpu)] &= ~(1ULL << cpuset_offset(cpu));
  set->ncpus--;
}

bool cpuset_test(struct cpuset *set, int cpu) {
  ASSERT(cpu < MAX_CPUS);
  return set->bits[cpuset_index(cpu)] & (1ULL << cpuset_offset(cpu));
}

int cpuset_next_set(struct cpuset *set, int cpu) {
  if (set->ncpus == 0) {
    return -1;
  }

  uint64_t mask;
  if (cpu >= 0) {
    mask = UINT64_MAX << cpuset_offset(cpu);
  } else {
    mask = UINT64_MAX;
  }

  for (int i = cpuset_index(cpu); i < CPUSET_MAX_INDEX; i++) {
    uint64_t bits = set->bits[i];
    if (bits == 0) {
      continue;
    }
    int index = bit_ffs64(bits & mask);
    return cpuset_cpu(i, index);
  }
  return -1;
}

//
//

void critical_enter() {
  thread_t *td = curthread;

  // disable interrupts
  uint64_t rflags;
  temp_irq_save(rflags);
  if (__expect_false(td == NULL)) {
    // no thread yet, save flags
    PERCPU_SET_RFLAGS(rflags);
    return;
  }

  if (atomic_fetch_add(&td->crit_level, 1) == 0) {
    // first time entering a critical section, save flags
    PERCPU_SET_RFLAGS(rflags);
  }
}

void critical_exit() {
  thread_t *td = curthread;
  if (__expect_false(td == NULL)) {
    uint64_t flags = PERCPU_RFLAGS;
    temp_irq_restore(flags);
    return;
  }

  if (atomic_fetch_sub(&td->crit_level, 1) == 1) {
    // last time exiting a critical section, restore flags
    uint64_t flags = PERCPU_RFLAGS;
    temp_irq_restore(flags);
  }
}

//
// MARK: System Calls
//

DEFINE_SYSCALL(getpid, pid_t) {
  return curproc->pid;
}

DEFINE_SYSCALL(getppid, pid_t) {
  return curproc->parent->pid;
}

DEFINE_SYSCALL(gettid, pid_t) {
  return curthread->tid;
}

DEFINE_SYSCALL(getuid, uid_t) {
  return curproc->creds->uid;
}

DEFINE_SYSCALL(geteuid, uid_t) {
  return curproc->creds->euid;
}

DEFINE_SYSCALL(getgid, gid_t) {
  return curproc->creds->gid;
}

DEFINE_SYSCALL(getegid, gid_t) {
  return curproc->creds->egid;
}

DEFINE_SYSCALL(getpgid, pid_t, pid_t pid) {
  DPRINTF("syscall: getpgid pid=%d\n", pid);
  proc_t *proc = proc_lookup(pid);
  if (proc == NULL) {
    return -ESRCH; // process not found
  }

  pr_lock(proc);
  pid_t pgid = proc->group->pgid;
  pr_unlock(proc);
  pr_putref(&proc);
  DPRINTF("syscall: getpgid -> res=%d\n", pgid);
  return pgid;
}

DEFINE_SYSCALL(setpgid, int, pid_t pid, pid_t pgid) {
  DPRINTF("syscall: setpgid pid=%d pgid=%d\n", pid, pgid);
  todo("setpgid: not implemented");
}

DEFINE_SYSCALL(getsid, pid_t, pid_t pid) {
  DPRINTF("syscall: getsid pid=%d\n", curproc->pid);
  proc_t *proc = proc_lookup(pid);
  if (proc == NULL) {
    return -ESRCH; // process not found
  }

  pr_lock(proc);
  pid_t sid = proc->group->session->sid;
  pr_unlock(proc);
  pr_putref(&proc);
  DPRINTF("syscall: getsid -> res=%d\n", sid);
  return sid;
}

DEFINE_SYSCALL(setsid, pid_t) {
  DPRINTF("syscall: setsid\n");
  proc_t *proc = curproc;
  pr_lock(proc);

  // check if the process is already a group leader
  if (proc_is_pgrp_leader(proc)) {
    EPRINTF("process {:pr} is already a group leader\n", proc);
    pr_unlock(proc);
    return -EPERM; // operation not permitted
  }

  // if not remove it from the current process group
  pgroup_t *pgrp = pgrp_getref(proc->group);
  pgrp_lock(pgrp);
  pgrp_remove_proc(pgrp, proc);
  pgrp_unlock(pgrp);
  pgrp_putref(&pgrp);

  // allocate a new process group and session
  pid_t sid = proc->pid; // sid == pgid == pid for the new session
  pgroup_t *new_pgrp = pgrp_alloc_add_proc(proc);
  pgrp_putref(&new_pgrp);
  DPRINTF("syscall: setsid -> new pgroup created: %d\n", proc->pid);
  pr_unlock(proc);
  return sid; // return session id (== process id)
}

DEFINE_SYSCALL(brk, unsigned long, unsigned long addr) {
  DPRINTF("syscall: brk addr=%p\n", (void *)addr);
  proc_t *proc = curproc;
  uintptr_t old_brk = proc->brk_end;
  DPRINTF("syscall: brk -> res=%p\n", (void *)old_brk);
  return old_brk;
}

DEFINE_SYSCALL(set_tid_address, long, const int *tidptr) {
  DPRINTF("syscall: set_tid_address tidptr=%p\n", tidptr);
  thread_t *td = curthread;
  DPRINTF("set_tid_address: TODO implement me\n");
  return td->tid;
}

DEFINE_SYSCALL(exit_group, void, int error_code) {
  DPRINTF("syscall: exit_group error_code=%d\n", error_code);
  proc_t *proc = curproc;
  proc_terminate(proc, error_code, 0);
}

DEFINE_SYSCALL(pause, int) {
  DPRINTF("syscall: pause\n");
  proc_t *proc = curproc;
  proc_wait_signal(proc);
  return -EINTR;
}

DEFINE_SYSCALL(wait4, pid_t, pid_t pid, int *status, int options, struct rusage *rusage) {
  DPRINTF("syscall: wait4 pid=%d status=%p options=%d rusage=%p\n", pid, status, options, rusage);
  if (
    (status != NULL && vm_validate_ptr((uintptr_t) status, /*write=*/true) < 0) ||
    (rusage != NULL && vm_validate_ptr((uintptr_t) rusage, /*write=*/true) < 0)
  ) {
    return -EFAULT; // invalid user pointer
  }
  return proc_syscall_wait4(pid, status, options, rusage);
}

DEFINE_SYSCALL(fork, pid_t) {
  DPRINTF("syscall: fork\n");
  proc_t *fork = proc_fork();
  thread_t *td = thread_syscall_fork();
  // forked thread execution starts at the syscall return
  proc_setup_add_thread(fork, td);
  proc_finish_setup_and_submit_all(fork);
  // parent process returns the child's pid
  return fork->pid;
}

DEFINE_SYSCALL(execve, int, const char *filename, char *const *argv, char *const *envp) {
  DPRINTF("syscall: execve filename=%s\n", filename);
  return proc_syscall_execve(cstr_make(filename), argv, envp);
}
