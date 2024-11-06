//
// Created by Aaron Gill-Braun on 2023-12-26.
//

#include <kernel/proc.h>
#include <kernel/sched.h>
#include <kernel/tqueue.h>
#include <kernel/clock.h>
#include <kernel/exec.h>
#include <kernel/mm.h>
#include <kernel/fs.h>

#include <kernel/printf.h>
#include <kernel/panic.h>
#include <kernel/bits.h>
#include <kernel/str.h>

#include <kernel/cpu/cpu.h>
#include <kernel/cpu/gdt.h>
#include <kernel/vfs/file.h>
#include <kernel/vfs/ventry.h>

#include <bitmap.h>

noreturn void idle_thread_entry();

extern uintptr_t entry_initial_stack;

// #define ASSERT(x)
#define ASSERT(x) kassert(x)
#define ASSERTF(x, fmt, ...) kassertf(x, fmt, ##__VA_ARGS__)
// #define DPRINTF(...)
#define DPRINTF(x, ...) kprintf("proc: " x, ##__VA_ARGS__)

#define goto_error(lbl, err) do { res = err; goto lbl; } while (0)

#define PROCS_MAX     1024
#define PROC_BRK_MAX  SIZE_16MB

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
  proc0 = proc_alloc_empty(0, NULL, getref(root_creds));

  // this lock is used to provide exclusive access to proc0 among the APs while they're
  // initializing and attaching their idle threads. they wont have a proc context at that
  // point therefore cant use pr_lock/_unlock.
  mtx_init(&proc0_ap_lock, MTX_SPIN, "proc0_ap_lock");

  thread_t *maintd = thread_alloc_empty(TDF_KTHREAD, (uintptr_t)&entry_initial_stack, SIZE_8KB);
  proc_setup_add_thread(proc0, maintd);
  pgrp_setup_add_proc(pgroup0, proc0);

  proc0->state = PRS_ACTIVE;
  maintd->state = TDS_RUNNING;

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

static inline proc_t *ptable_get_proc(pid_t pid) {
  ASSERT(pid < PROCS_MAX);
  struct ptable_entry *entry = &_ptable.entries[pid_index(pid)];
  mtx_lock(&entry->lock);
  LIST_FOR_IN(proc, &entry->head, hashlist) {
    if (proc->pid == pid) {
      mtx_unlock(&entry->lock);
      return proc;
    }
  }

  mtx_unlock(&entry->lock);
  return NULL;
}

static inline void ptable_add_proc(pid_t pid, proc_t *proc) {
  ASSERT(pid < PROCS_MAX);
  struct ptable_entry *entry = &_ptable.entries[pid_index(pid)];
  mtx_spin_lock(&entry->lock);
  LIST_INSERT_ORDERED_BY(&entry->head, proc, hashlist, pid);
  atomic_fetch_add(&_ptable.nprocs, 1);
  mtx_spin_unlock(&entry->lock);
}

static inline void ptable_remove_proc(pid_t pid, proc_t *proc) {
  ASSERT(pid < PROCS_MAX);
  struct ptable_entry *entry = &_ptable.entries[pid_index(pid)];
  mtx_spin_lock(&entry->lock);
  LIST_REMOVE(&entry->head, proc, hashlist);
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

  ptable_add_proc(0, proc0);
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

void pcreds_release(__move struct pcreds **credsp) {
  putref(credsp, kfree);
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
  td->proc = proc;
  td->creds = getref(proc->creds);
  td->tid = (pid_t)proc->num_threads;
  LIST_ADD(&proc->threads, td, plist);
  proc->num_threads++;
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

proc_t *proc_alloc_empty(pid_t pid, struct address_space *space, struct pcreds *creds) {
  proc_t *proc = kmallocz(sizeof(proc_t));
  proc->pid = pid;
  proc->space = space;
  proc->creds = getref(creds);
  proc->pwd = fs_root_getref();

  proc->files = ftable_alloc();
  proc->sigacts = NULL; // TODO
  proc->usage = kmallocz(sizeof(struct rusage));
  proc->limit = kmallocz(sizeof(struct rlimit));
  proc->stats = kmallocz(sizeof(struct pstats));

  proc->state = PRS_EMPTY;

  mtx_init(&proc->lock, 0, "proc_lock");
  mtx_init(&proc->statlock, MTX_SPIN, "proc_statlock");
  cond_init(&proc->td_exit_cond, "td_exit_cond");
  return proc;
}

void proc_free_exited(proc_t **procp) {
  proc_t *proc = *procp;
  ASSERT(PRS_IS_EXITED(proc));
  proc_free_pid(proc->pid);
  pcreds_release(&proc->creds);
  ve_release(&proc->pwd);

  ftable_free(proc->files);
  proc->files = NULL;

  kfreep(&proc->usage);
  kfreep(&proc->limit);
  kfreep(&proc->stats);

  mtx_destroy(&proc->lock);
  mtx_destroy(&proc->statlock);
  cond_destroy(&proc->td_exit_cond);

  kfree(proc);
  *procp = NULL;
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
  if ((res = exec_image_setup_stack(image, 0x8000000, SIZE_16KB, proc->creds, proc->args, proc->env, &stack)) < 0)
    goto ret;

  // place the process `brk` mapping after the last data segment
  vm_desc_t *last_segment = SLIST_GET_LAST(image->descs, next);
  uintptr_t last_segment_end = last_segment->address + last_segment->size;

  // map the image(s) and stack descriptors into the new process space
  if (vm_desc_map_space(proc->space, image->descs)< 0) {
    DPRINTF("failed to map image descriptors\n");
    goto_error(ret, -ENOMEM);
  }
  if (image->interp && vm_desc_map_space(proc->space, image->interp->descs) < 0) {
    DPRINTF("failed to map interpreter descriptors\n");
    goto_error(ret, -ENOMEM);
  }
  if (vm_desc_map_space(proc->space, stack->descs) < 0) {
    DPRINTF("failed to map stack descriptors\n");
    goto_error(ret, -ENOMEM);
  }

  // create a mapping for the process `brk` segment that reserves the virtual space
  // but initially has no size. the segment will be expanded as needed by the process
  if (vmap_other_anon(proc->space, PROC_BRK_MAX, last_segment_end, 0, VM_RDWR|VM_FIXED, "brk") == 0) {
    DPRINTF("failed to map brk segment\n");
    goto_error(ret, -ENOMEM);
  }

  proc->binpath = str_move(image->path);
  proc->brk_start = last_segment_end;
  proc->brk_end = last_segment_end;
  proc->brk_max = last_segment_end + PROC_BRK_MAX;

  thread_t *td = pr_main_thread(proc);
  td->tcb->rip = image->interp ? image->interp->entry : image->entry;
  td->tcb->rsp = stack->base + stack->off;
  td->tcb->rflags = 0x3202; // IF=1, IOPL=3
  td->tcb->tcb_flags |= TCB_SYSRET;

  res = 0; // success
LABEL(ret);
  exec_free_image(&image);
  exec_free_stack(&stack);
  return res;
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

void proc_add_thread(proc_t *proc, thread_t *td) {
  ASSERT(TDS_IS_EMPTY(td));
  ASSERT(td->proc == NULL);

  pr_lock(proc);
  ASSERT(PRS_IS_ALIVE(proc));

  proc_do_add_thread(proc, td);
  pr_unlock(proc);
}

void proc_exit_all_wait(proc_t *proc, int exit_code) {
  pr_lock(proc);
  ASSERT(PRS_IS_ALIVE(proc));
  DPRINTF("proc %d exiting with code %d\n", proc->pid, exit_code);

  // TODO: handle wait()'d processes
  proc->state = PRS_EXITED;
  proc->exit_code = exit_code;
  proc->num_exiting = proc->num_threads;
  // TODO: ascynchronously stop threads then wait for all to exit
  if (proc == curproc) {
    // stop all other threads first
    LIST_FOR_IN(td, &proc->threads, plist) {
      if (td != curthread) {
        thread_stop(td);
      }
    }

    thread_stop(curthread);
  } else {
    LIST_FOR_IN(td, &proc->threads, plist) {
      thread_stop(td);
    }
  }

  ASSERT(proc->num_exiting == 0);
  ASSERT(proc->num_exited == proc->num_threads);
  DPRINTF("proc %d exited\n", proc->pid);
  pr_unlock(proc);
}

//


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

static void thread_start_wrapper() {
  proc_t *proc = curproc;
  thread_t *td = curthread;
  kprintf("hello from thread %d.%d\n", proc->pid, td->tid);
  todo();

  thread_stop(td);
  unreachable;
}

// thread api

thread_t *thread_alloc_idle() {
  // this is called once by each cpu during scheduler initialization
  thread_t *td = thread_alloc_empty(TDF_KTHREAD|TDF_IDLE, 0, SIZE_8KB);
  td->state = TDS_READY;
  td->name = str_fmt("idle thread [CPU#%d]", curcpu_id);
  td->tcb->rip = (uintptr_t) idle_thread_entry;

  mtx_spin_lock(&proc0_ap_lock);
  proc_do_add_thread(proc0, td);
  mtx_spin_unlock(&proc0_ap_lock);
  return td;
}

thread_t *thread_alloc_empty(uint32_t flags, uintptr_t kstack_base, size_t kstack_size) {
  ASSERT(is_aligned(kstack_base, PAGE_SIZE));
  ASSERT(kstack_size > 0 && is_aligned(kstack_size, PAGE_SIZE));

  thread_t *td = kmallocz(sizeof(thread_t));
  td->flags = flags;
  td->flags2 = TDF2_FIRSTTIME;

  if (kstack_base == 0) {
    // allocate a new kernel stack
    page_t *pages = alloc_pages(SIZE_TO_PAGES(kstack_size));
    ASSERT(pages != NULL);
    td->kstack_base = vmap_pages(moveref(pages), 0, kstack_size, VM_RDWR|VM_STACK, "kstack");
    td->kstack_size = kstack_size;
  } else {
    // use base as the kernel stack
    td->kstack_base = kstack_base;
    td->kstack_size = kstack_size;
  }

  td->cpuset = cpuset_alloc(NULL);
  td->own_lockq = lockq_alloc();
  td->own_waitq = waitq_alloc();
  td->wait_claims = lock_claim_list_alloc();

  td->state = TDS_EMPTY;
  td->cpu_id = -1;
  td->pri_base = td_flags_to_base_priority(flags);
  td->priority = td->pri_base;

  // kstack top   --------------
  //        tcb ->
  //  trapframe ->
  //                   ...
  // kstack base  --------------
  uintptr_t kstack_top = td->kstack_base + td->kstack_size;
  kstack_top -= align(sizeof(struct tcb), 16);
  td->tcb = (struct tcb *) kstack_top;
  memset(td->tcb, 0, sizeof(struct tcb));
  kstack_top -= sizeof(struct trapframe);
  td->frame = (struct trapframe *) kstack_top;
  memset(td->frame, 0, sizeof(struct trapframe));

  if (flags & TDF_KTHREAD) {
    td->tcb->tcb_flags |= TCB_KERNEL; // kernel thread
  } else {
    td->tcb->tcb_flags |= TCB_IRETQ; // user thread
  }

  td->tcb->rip = (uintptr_t) thread_start_wrapper;
  td->tcb->rsp = kstack_top;

  mtx_init(&td->lock, 0, "thread_lock");
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
  td_unlock(td);
  // td is now owned by scheduler
}

void thread_stop(thread_t *td) {
  ASSERT(!TDS_IS_EXITED(td));
  td->flags2 |= TDF2_STOPPING;
  td_lock_assert(td, MA_NOTOWNED);
  td_lock(td);

  proc_t *proc = td->proc;
  atomic_fetch_add(&proc->num_exiting, 1);
  if (TDS_IS_RUNNING(td)) {
    if (td == curthread) {
      // stop the current thread
      sched_again(SCHED_EXITED);
      unreachable;
    }
    // stop thread running on another cpu
    sched_cpu(td->cpu_id, SCHED_EXITED);
    td_unlock(td);
    return;
  }

  // thread isnt currently active
  if (TDS_IS_READY(td)) {
    // remove ready thread from the scheduler
    sched_remove_ready_thread(td);
  } else if (TDS_IS_BLOCKED(td)) {
    // remove blocked thread from lockqueue
    lockq_chain_lock(td->lockobj);
    struct lockqueue *lq = lockq_lookup(td->lockobj);
    lockq_remove(lq, td, td->lockq_num);
    lockq_chain_unlock(td->lockobj);

    td->lockobj = NULL;
    td->lockq_num = -1;
  } else if (TDS_IS_WAITING(td)) {
    // remove thread from the associated waitqueue
    waitq_chain_lock(td->wchan);
    struct waitqueue *wq = waitq_lookup(td->wchan);
    waitq_remove(wq, td);
    waitq_chain_unlock(td->wchan);

    td->wchan = NULL;
    td->wdmsg = NULL;
  }

  TD_SET_STATE(td, TDS_EXITED);

  atomic_fetch_add(&proc->num_exited, 1);
  cond_signal(&proc->td_exit_cond);

  td_unlock(td);
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
// MARK: Syscalls
//

// DEFINE_SYSCALL(brk, unsigned long, unsigned long addr) {
//   DPRINTF("brk: addr=%p\n", (void *)addr);
//   proc_t *proc = curproc;
//   uintptr_t brk_start = proc->brk_start;
//   uintptr_t old_brk = proc->brk_end;
//
//   uintptr_t new_brk = (uintptr_t)addr;
//   if (new_brk == 0 || !is_aligned(new_brk, PAGE_SIZE) || !(new_brk >= brk_start && new_brk <= proc->brk_max)) {
//     return old_brk;
//   }
//
//   size_t old_size = old_brk - brk_start;
//   size_t new_size = new_brk - brk_start;
//   int res;
//
//   // attempt to resize the brk mapping
//   if ((res = vmap_resize(brk_start, old_size, new_size, /*allow_move=*/false, NULL)) < 0) {
//     DPRINTF("failed to resize brk segment proc=%d {:err}\n", proc->pid, res);
//     return old_brk;
//   }
//
//   DPRINTF("brk: res=%p\n", new_brk);
//   proc->brk_end = new_brk;
//   return new_brk;
// }

DEFINE_SYSCALL(brk, unsigned long, unsigned long addr) {
  DPRINTF("brk: addr=%p\n", (void *)addr);
  proc_t *proc = curproc;
  uintptr_t brk_start = proc->brk_start;
  uintptr_t old_brk = proc->brk_end;
  DPRINTF("brk: res=%p\n", (void *)old_brk);
  return old_brk;
}

DEFINE_SYSCALL(set_tid_address, long, const int *tidptr) {
  DPRINTF("set_tid_address: tidptr=%p\n", tidptr);
  thread_t *td = curthread;
  // td->tidptr = tidptr;
  return td->tid;
}

DEFINE_SYSCALL(exit_group, void, int error_code) {
  DPRINTF("exit_group: error_code=%d\n", error_code);
  proc_t *proc = curproc;
  proc_exit_all_wait(proc, error_code);
}
