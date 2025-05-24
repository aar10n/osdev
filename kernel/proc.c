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

// #define ASSERT(x)
#define ASSERT(x) kassert(x)
#define ASSERTF(x, fmt, ...) kassertf(x, fmt, ##__VA_ARGS__)
// #define DPRINTF(...)
#define DPRINTF(x, ...) kprintf("proc: " x, ##__VA_ARGS__)

#define goto_res(lbl, err) do { res = err; goto lbl; } while (0)

#define PROCS_MAX     1024
#define PROC_BRK_MAX  SIZE_16MB

#define PROC_USTACK_BASE 0x8000000
#define PROC_USTACK_SIZE SIZE_16KB

proc_t *proc_alloc_internal(pid_t pid, struct address_space *space, struct pcreds *creds, struct ventry *pwd);

static struct pcreds *root_creds;
static pgroup_t *pgroup0;
static proc_t *proc0;
static mtx_t proc0_ap_lock;

struct percpu *percpu0;

void proc0_init() {
  // runs just after the early initializers
  percpu0 = curcpu_area;

  root_creds = pcreds_alloc(0, 0);
  pgroup0 = pgrp_alloc_empty(0, NULL);
  proc0 = proc_alloc_internal(0, NULL, getref(root_creds), fs_root_getref());

  // this lock is used to provide exclusive access to proc0 among the APs while they're
  // initializing and attaching their idle threads. they wont have a proc context at that
  // point therefore cant use pr_lock/_unlock.
  mtx_init(&proc0_ap_lock, MTX_SPIN, "proc0_ap_lock");

  thread_t *maintd = thread_alloc_proc0_main();
  proc_setup_add_thread(proc0, maintd);
  pgrp_setup_add_proc(pgroup0, proc0);

  proc0->state = PRS_ACTIVE;

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
  ASSERT(pid < PROCS_MAX);
  struct ptable_entry *entry = &_ptable.entries[pid_index(pid)];
  mtx_spin_lock(&entry->lock);
  LIST_REMOVE(&entry->head, moveref(proc), hashlist);
  atomic_fetch_sub(&_ptable.nprocs, 1);
  mtx_spin_unlock(&entry->lock);
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

static struct pstrings *pstrings_alloc(const char *const strings[]) {
  if (strings == NULL)
    return NULL;

  size_t count = 0;
  size_t full_size = 0;
  while (strings[count] != NULL) {
    full_size += strlen(strings[count]) + 1;
    count++;
  }

  if (count == 0)
    return NULL;
  if (full_size > SIZE_1MB) {
    DPRINTF("pstrings_alloc: strings too large\n");
    return NULL;
  }

  page_t *pages = alloc_pages(SIZE_TO_PAGES(full_size));
  if (pages == NULL) {
    DPRINTF("pstrings_alloc: failed to allocate pages\n");
    return NULL;
  }

  size_t aligned_size = page_align(full_size);
  void *kptr = (void *) vmap_pages(getref(pages), 0, aligned_size, VM_RDWR, "pstrings");
  if (kptr == NULL) {
    DPRINTF("pstrings_alloc: failed to map pages\n");
    drop_pages(&pages);
    return NULL;
  }

  struct pstrings *pstrings = kmallocz(sizeof(struct pstrings));
  pstrings->count = count;
  pstrings->size = full_size;
  pstrings->pages = pages;
  pstrings->kptr = kptr;

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

  return pstrings;
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

///////////////////
// MARK: pgroup

pgroup_t *pgrp_alloc_empty(pid_t pgid, session_t *session) {
  pgroup_t *pgroup = kmallocz(sizeof(pgroup_t));
  pgroup->pgid = pgid;
  pgroup->session = session;
  pgroup->num_procs = 0;
  LIST_INIT(&pgroup->procs);
  mtx_init(&pgroup->lock, 0, "pgroup_lock");
  return pgroup;
}

void pgrp_free_empty(pgroup_t **pgp) {
  pgroup_t *pgroup = *pgp;
  ASSERT(pgroup->num_procs == 0);
  mtx_destroy(&pgroup->lock);
  kfree(pgroup);
  *pgp = NULL;
}

void pgrp_setup_add_proc(pgroup_t *pg, proc_t *proc) {
  ASSERT(PRS_IS_EMPTY(proc));
  ASSERT(proc->group == NULL);

  proc->group = pg;
  pg->num_procs++;
  LIST_ADD(&pg->procs, proc, pglist);
}

void pgrp_add_proc(pgroup_t *pg, proc_t *proc) {
  ASSERT(PRS_IS_ALIVE(proc));
  pgrp_lock_assert(pg, MA_OWNED);
  pr_lock_assert(proc, MA_OWNED);

  LIST_ADD(&pg->procs, proc, pglist);
  pg->num_procs++;
  proc->group = pg;
}

void pgrp_remove_proc(pgroup_t *pg, proc_t *proc) {
  pgrp_lock_assert(pg, MA_OWNED);
  pr_lock_assert(proc, MA_OWNED);

  proc->group = NULL;
  pg->num_procs--;
  LIST_REMOVE(&pg->procs, proc, pglist);
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
  pr_putref(&td->proc);
  pcreds_release(&td->creds);
  LIST_REMOVE(&proc->threads, td, plist);
  proc->num_threads--;
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

__ref proc_t *proc_alloc_internal(pid_t pid, struct address_space *space, struct pcreds *creds, struct ventry *pwd) {
  proc_t *proc = kmallocz(sizeof(proc_t));
  proc->pid = pid;
  proc->space = space;
  proc->creds = getref(creds);
  proc->pwd = fs_root_getref();

  proc->files = ftable_alloc();
  proc->usage = kmallocz(sizeof(struct rusage));
  proc->limit = kmallocz(sizeof(struct rlimit));
  proc->stats = kmallocz(sizeof(struct pstats));
  proc->sigacts = sigacts_alloc();
  sigqueue_init(&proc->sigqueue);

  proc->state = PRS_EMPTY;
  ref_init(&proc->refcount);

  mtx_init(&proc->lock, MTX_RECURSIVE, "proc_lock");
  mtx_init(&proc->statlock, MTX_SPIN, "proc_statlock");
  cond_init(&proc->td_exit_cond, "td_exit_cond");
  cond_init(&proc->signal_cond, "signal_cond");
  return proc; __ref
}

__ref proc_t *proc_alloc_new(struct pcreds *creds) {
  return proc_alloc_internal(
    proc_alloc_pid(),
    vm_new_empty_space(),
    creds ? creds : pcreds_alloc(0, 0),
    fs_root_getref()
  );
}

__ref proc_t *proc_fork(proc_t *proc) {
  ASSERT(PRS_IS_ALIVE(proc));
  mtx_lock(&proc->lock);
  address_space_t *space = proc->space;

  mtx_lock(&space->lock);
  address_space_t *new_space = vm_new_fork_space(space, /*deepcopy_user=*/true);
  mtx_unlock(&space->lock);

  proc_t *new_proc = proc_alloc_internal(proc_alloc_pid(), new_space, getref(proc->creds), ve_getref(proc->pwd));
  ASSERT(new_proc != NULL);

  new_proc->files = ftable_clone(proc->files);
  new_proc->usage = kmalloc_cp(proc->usage, sizeof(struct rusage));
  new_proc->limit = kmalloc_cp(proc->limit, sizeof(struct rlimit));
  new_proc->stats = kmalloc_cp(proc->stats, sizeof(struct pstats));
  new_proc->sigacts = sigacts_clone(proc->sigacts);

  // TODO: replaece existing vm mappings for the arg and env pages with COW versions
  new_proc->args = pstrings_copy(proc->args);
  new_proc->env = pstrings_copy(proc->env);
  new_proc->binpath = str_dup(proc->binpath);

  new_proc->brk_start = proc->brk_start;
  new_proc->brk_end = proc->brk_end;
  new_proc->brk_max = proc->brk_max;

  new_proc->name = str_dup(proc->name);
  new_proc->ppid = proc->pid;

  mtx_unlock(&proc->lock);
  return new_proc; __ref
}

void proc_cleanup(__move proc_t **procp) {
  proc_t *proc = moveref(*procp);
  pr_lock_assert(proc, LA_LOCKED);
  ASSERT(PRS_IS_EXITED(proc));

  proc_free_pid(proc->pid);
  pcreds_release(&proc->creds);
  ve_release(&proc->pwd);

  ftable_free(moveptr(proc->files));
  sigacts_free(&proc->sigacts);

  kfreep(&proc->usage);
  kfreep(&proc->limit);
  kfreep(&proc->stats);

  mtx_destroy(&proc->lock);
  mtx_destroy(&proc->statlock);
  cond_destroy(&proc->td_exit_cond);

  kfree(proc);
}

void proc_setup_add_thread(proc_t *proc, thread_t *td) {
  ASSERT(PRS_IS_EMPTY(proc));
  ASSERT(TDS_IS_EMPTY(td))
  ASSERT(td->proc == NULL);
  proc_do_add_thread(proc, td);
}

int proc_setup_new_env(proc_t *proc, const char *const env[]) {
  ASSERT(PRS_IS_EMPTY(proc));
  ASSERT(proc->env == NULL);
  proc->env = pstrings_alloc(env);
  if (proc->env == NULL) {
    return -ENOMEM;
  }
  return 0;
}

int proc_setup_copy_env(proc_t *proc, struct pstrings *env) {
  ASSERT(PRS_IS_EMPTY(proc));
  ASSERT(proc->env == NULL);
  proc->env = pstrings_copy(env);
  if (proc->env == NULL) {
    return -ENOMEM;
  }
  return 0;
}

int proc_setup_exec_args(proc_t *proc, const char *const args[]) {
  ASSERT(PRS_IS_EMPTY(proc));
  ASSERT(proc->args == NULL);
  proc->args = pstrings_alloc(args);
  if (proc->args == NULL) {
    return -ENOMEM;
  } else if (proc->args->count > ARG_MAX) {
    pstrings_free(&proc->args);
    return -E2BIG;
  }
  return 0;
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
    DPRINTF("failed to map image descriptors\n");
    goto_res(ret, -ENOMEM);
  }
  if (image->interp && vm_desc_map_space(proc->space, image->interp->descs) < 0) {
    DPRINTF("failed to map interpreter descriptors\n");
    goto_res(ret, -ENOMEM);
  }
  if (vm_desc_map_space(proc->space, stack->descs) < 0) {
    DPRINTF("failed to map stack descriptors\n");
    goto_res(ret, -ENOMEM);
  }

  // create a mapping for the process `brk` segment that reserves the virtual space
  // but initially has no size. the segment will be expanded as needed by the process
  if (vmap_other_anon(proc->space, PROC_BRK_MAX, last_segment_end, 0, VM_RDWR|VM_FIXED, "brk") == 0) {
    DPRINTF("failed to map brk segment\n");
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

int proc_setup_entry(proc_t *proc, void (*function)(void)) {
  ASSERT(PRS_IS_EMPTY(proc));
  ASSERT(pr_main_thread(proc) != NULL);
  ASSERT(proc->args == NULL);
  ASSERT(proc->env == NULL);

  thread_t *td = pr_main_thread(proc);
  thread_setup_entry(td, function);
  return 0;
}

int proc_setup_clone_fds(proc_t *proc, struct ftable *ftable) {
  ASSERT(PRS_IS_EMPTY(proc));
  ASSERT(ftable_empty(proc->files));
  ftable_free(proc->files);
  proc->files = ftable_clone(ftable);
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

void proc_add_thread(proc_t *proc, thread_t *td) {
  ASSERT(TDS_IS_EMPTY(td));
  ASSERT(td->proc == NULL);

  pr_lock(proc);
  if (PRS_IS_EXITED(proc)) {
    DPRINTF("proc_add_thread: called on dead process %d\n", proc->pid);
    pr_unlock(proc);
    return;
  }

  proc_do_add_thread(proc, td);
  pr_unlock(proc);
}

void proc_kill(proc_t *proc, int exit_code) {
  pr_lock(proc);
  if (PRS_IS_EXITED(proc)) {
    DPRINTF("proc_exit_all_wait: called on dead process %d\n", proc->pid);
    pr_unlock(proc);
    return;
  }

  DPRINTF("proc %d terminating with code %d\n", proc->pid, exit_code);
  proc->state = PRS_EXITED;
  proc->exit_code = exit_code;
  proc->num_exiting = proc->num_threads;

  if (proc->pending_alarm > 0) {
    // cancel pending signal alarm
    alarm_unregister(proc->pending_alarm);
    proc->pending_alarm = 0;
  }

  if (proc == curproc) {
    // terminate other threads first
    LIST_FOR_IN(td, &proc->threads, plist) {
      if (td != curthread) {
        thread_kill(td);
      }
    }

    pr_unlock(proc);
    // exit the current thread
    thread_kill(curthread);
    unreachable;
  }

  // terminate all the threads
  LIST_FOR_IN(td, &proc->threads, plist) {
    thread_kill(td);
  }

  // cancel pending alarms

  pr_unlock(proc);
}

void proc_stop(proc_t *proc) {
  pr_lock(proc);
  if (PRS_IS_EXITED(proc)) {
    DPRINTF("proc_stop: called on dead process %d\n", proc->pid);
    pr_unlock(proc);
    return;
  }

  DPRINTF("proc %d stopping\n", proc->pid);
  atomic_fetch_or(&proc->flags, PRF_STOPPED);
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
  if (PRS_IS_EXITED(proc)) {
    DPRINTF("proc_cont: called on dead process %d\n", proc->pid);
    pr_unlock(proc);
    return;
  }

  DPRINTF("proc %d continuing\n", proc->pid);
  atomic_fetch_and(&proc->flags, ~PRF_STOPPED);

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
  pr_lock(proc);
  if (!PRS_IS_ALIVE(proc)) {
    DPRINTF("proc_signal: called on dead process %d\n", proc->pid);
    goto_res(ret, -ESRCH);
  }

  struct sigaction sa;
  enum sigdisp disp;
  if ((res = sigacts_get(proc->sigacts, sig, &sa, &disp)) < 0) {
    goto ret;
  }

  if (disp == SIGDISP_IGN) {
    DPRINTF("signal %d ignored by proc %d\n", sig, proc->pid);
    goto_res(ret, 0);
  }

  if (disp == SIGDISP_TERM) {
    proc_kill(proc, 128 + sig);
    goto_res(ret, 0);
  } else if (disp == SIGDISP_STOP) {
    proc_stop(proc);
    goto_res(ret, 0);
  } else if (disp == SIGDISP_CONT) {
    proc_cont(proc);
    goto_res(ret, 0);
  } else if (disp == SIGDISP_CORE) {
    todo("proc_signal: core dump");
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
  thread_setup_entry(td, idle_thread_entry);

  mtx_spin_lock(&proc0_ap_lock);
  proc_do_add_thread(proc0, td);
  mtx_spin_unlock(&proc0_ap_lock);
  return td;
}

void thread_free_exited(thread_t **tdp) {
  thread_t *td = *tdp;
  ASSERT(TDS_IS_EXITED(td));
  td_lock_assert(td, MA_UNLOCKED);

  mtx_destroy(&td->lock);
  kfree(td->frame);
  td->frame = NULL;
  lock_claim_list_free(&td->wait_claims);
  lockq_free(&td->own_lockq);
  waitq_free(&td->own_waitq);
  cpuset_free(&td->cpuset);

  pcreds_release(&td->creds);
  // tcb_free(&td->tcb);
  todo();
  kfree(td);
  *tdp = NULL;
}

int thread_setup_entry(thread_t *td, void (*entry)(void)) {
  ASSERT(TDS_IS_EMPTY(td));
  ASSERT(td->tcb->rip == 0);

  td->tcb->rip = (uintptr_t) entry;
  if (is_userspace_ptr((uintptr_t) entry)) {
    td->tcb->tcb_flags |= TCB_SYSRET;
  } else {
    td->tcb->tcb_flags |= TCB_KERNEL;
  }

  if (!str_isnull(td->name)) {
    td->name = str_from_charp(debug_function_name((uintptr_t) entry));
  }

  td->tcb->rip = (uintptr_t) entry;
  if (TDF_IS_KTHREAD(td)) {
    // this is a kernel thread
    if (!is_kernel_code_ptr((uintptr_t) entry)) {
      DPRINTF("warning: kernel thread entry point is not a kernel code pointer\n");
    }

    // this is a kernel thread so we can use the kernel stack
    td->tcb->rsp = thread_get_kstack_top(td);
    td->tcb->rflags = 0x202; // IF=1, IOPL=0
  } else {
    // this is a user thread
    if (!is_userspace_ptr((uintptr_t) entry)) {
      DPRINTF("warning: user thread entry point is not a userspace pointer\n");
    }

    if (td->ustack_base == 0) {
      // allocate a new user stack for the thread
      page_t *ustack_pages = alloc_pages(SIZE_TO_PAGES(PROC_USTACK_SIZE));
      if (ustack_pages == NULL) {
        DPRINTF("failed to allocate user stack pages\n");
        return -ENOMEM;
      }

      uintptr_t ustack_base = vmap_pages(
        moveref(ustack_pages),
        PROC_USTACK_BASE,
        PROC_USTACK_SIZE,
        VM_RDWR|VM_USER|VM_STACK,
        "user stack"
      );
      if (ustack_base == 0) {
        DPRINTF("failed to allocate user stack\n");
        return -ENOMEM;
      }

      td->ustack_base = ustack_base;
      td->ustack_size = PROC_USTACK_SIZE;
    }

    td->tcb->rsp = td->ustack_base + td->ustack_size;
    td->tcb->rflags |= 0x3202; // IF=1, IOPL=3
  }
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
  td_lock_assert(td, MA_NOTOWNED);

  td_lock(td);
  if (TDS_IS_EXITED(td)) {
    DPRINTF("thread_kill: thread already exited {:td}\n", td);
    goto done;
  }

  atomic_fetch_add(&td->proc->num_exiting, 1);
  atomic_fetch_or(&td->flags2, TDF2_STOPPED);
  if (TDS_IS_RUNNING(td)) {
    if (td == curthread) {
      // stop the current thread
      sched_again(SCHED_EXITED);
      unreachable;
    }

    // stop thread running on another cpu
    sched_cpu(td->cpu_id, SCHED_EXITED);
    goto done;
  }

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
  atomic_fetch_add(&td->proc->num_exited, 1);
  cond_broadcast(&td->proc->td_exit_cond);

LABEL(done);
  td_unlock(td);
}

void thread_stop(thread_t *td) {
  td_lock_assert(td, MA_NOTOWNED);

  td_lock(td);
  if (TDS_IS_EXITED(td)) {
    DPRINTF("thread_stop: thread already exited {:td}\n", td);
    goto done;
  } else if (TDF2_IS_STOPPED(td)) {
    DPRINTF("thread_stop: thread already stopped {:td}\n", td);
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
    DPRINTF("thread_cont: thread has exited {:td}\n", td);
    goto done;
  } else if (!TDF2_IS_STOPPED(td)) {
    DPRINTF("thread_stop: thread is not stopped {:td}\n", td);
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
  return curproc->ppid;
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

DEFINE_SYSCALL(brk, unsigned long, unsigned long addr) {
  DPRINTF("syscall: brk addr=%p\n", (void *)addr);
  proc_t *proc = curproc;
  uintptr_t brk_start = proc->brk_start;
  uintptr_t old_brk = proc->brk_end;
  DPRINTF("syscall: brk -> res=%p\n", (void *)old_brk);
  return old_brk;
}

DEFINE_SYSCALL(set_tid_address, long, const int *tidptr) {
  DPRINTF("syscall: set_tid_address tidptr=%p\n", tidptr);
  thread_t *td = curthread;
  // td->tidptr = tidptr;
  return td->tid;
}

DEFINE_SYSCALL(exit_group, void, int error_code) {
  DPRINTF("syscall: exit_group error_code=%d\n", error_code);
  proc_t *proc = curproc;
  proc_kill(proc, error_code);
}

DEFINE_SYSCALL(pause, int) {
  DPRINTF("syscall: pause\n");
  proc_t *proc = curproc;
  proc_wait_signal(proc);
  return -EINTR;
}
