//
// Created by Aaron Gill-Braun on 2023-12-26.
//

#include <kernel/proc.h>
#include <kernel/mm.h>
#include <kernel/lock.h>
#include <kernel/tqueue.h>
#include <kernel/panic.h>

#include <kernel/cpu/cpu.h>
#include <asm/bits.h>

#include <bitmap.h>

// #define ASSERT(x)
#define ASSERT(x) kassert(x)
// #define DPRINTF(...)
#define DPRINTF(x, ...) kprintf("proc: " x, ##__VA_ARGS__)

#define MAX_PROCS 8192

//
// MARK: proc
//

// MARK: Process table
static struct ptable {
  bitmap_t *pidset;   // allocatable pid set
  proc_t **array;     // array of process pointers
  size_t nprocs;      // number of processes
  mtx_t lock;         // process table lock
} ptable;

static void ptable_init() {
  size_t ptable_size = align(MAX_PROCS*sizeof(void *), PAGE_SIZE);
  uintptr_t ptable_base = vmap_anon(0, 0, ptable_size, VM_WRITE|VM_GLOBAL, "ptable");
  kassert(ptable_base != 0);

  ptable.pidset = create_bitmap(MAX_PROCS);
  ptable.array = (void *) ptable_base;
  ptable.nprocs = 0;
  mtx_init(&ptable.lock, 0, "ptable_lock");
}
STATIC_INIT(ptable_init);

//

void proc_init() {

}

pid_t proc_alloc_pid() {
  mtx_lock(&ptable.lock);
  index_t pid = bitmap_get_set_free(ptable.pidset);
  if (pid == -1) {
    mtx_unlock(&ptable.lock);
    return -1;
  }

  bitmap_set(ptable.pidset, pid);
  mtx_unlock(&ptable.lock);
  return (pid_t)pid;
}

void proc_free_pid(pid_t pid) {
  mtx_lock(&ptable.lock);
  bitmap_clear(ptable.pidset, pid);
  mtx_unlock(&ptable.lock);
}

proc_t *proc_alloc_empty(pid_t pid, struct address_space *space, pgroup_t *group, struct creds *creds) {
  proc_t *proc = kmallocz(sizeof(proc_t));
  proc->space = space;
  proc->group = group;
  proc->creds = getref(creds);

  proc->pid = pid;
  proc->ppid = 0;
  proc->state = PRS_EMPTY;

  mtx_init(&proc->lock, 0, "proc_lock");
  mtx_init(&proc->statlock, 0, "proc_statlock");
  return proc;
}

void proc_free_exited(proc_t **procp) {
  proc_t *proc = *procp;
  ASSERT(proc->state == PRS_EXITED);

  proc_free_pid(proc->pid);
  mtx_destroy(&proc->lock);
  mtx_destroy(&proc->statlock);
  kfree(proc);
  *procp = NULL;
}

void proc_setup_add_thread(proc_t *proc, thread_t *td) {
  ASSERT(proc->state == PRS_EMPTY);
  ASSERT(td->state == TDS_EMPTY);
  ASSERT(td->proc == NULL);

  td->proc = proc;
  td->tid = (pid_t)proc->num_threads;
  LIST_ADD(&proc->threads, td, plist);
  proc->num_threads++;
}



//
// MARK: thread
//

static inline uint8_t td_flags_to_base_priority(uint32_t td_flags) {
  if (td_flags & TDF_ITHREAD) {
    return PRI_REALTIME;
  } else if (td_flags & TDF_IDLE) {
    return PRI_IDLE;
  } else {
    return PRI_NORMAL;
  }
}

thread_t *thread_alloc_empty(uint32_t flags, proc_t *proc, struct creds *creds) {
  thread_t *td = kmallocz(sizeof(thread_t));
  td->flags = flags;
  td->lock = mtx_alloc(MTX_RECURSE, "td_lock");
  td->tcb = tcb_alloc((flags & TDF_KTHREAD) ? TCB_KERNEL : TCB_IRETQ);
  td->proc = proc;
  td->creds = getref(creds);

  td->cpuset = cpuset_alloc(NULL);
  td->own_lockq = lockq_alloc();
  td->own_waitq = waitq_alloc();
  td->lock_claims = lock_claim_list_alloc();

  td->state = TDS_EMPTY;
  td->pri_base = td_flags_to_base_priority(flags);
  td->priority = td->pri_base;
  return td;
}

void thread_free_exited(thread_t **tdp) {
  thread_t *td = *tdp;
  ASSERT(td->state == TDS_EXITED);

  cpuset_free(td->cpuset);
  lockq_free(&td->own_lockq);
  waitq_free(&td->own_waitq);
  lock_claim_list_free(&td->lock_claims);
  tcb_free(&td->tcb);
  mtx_free(td->lock);
  kfree(td);
  *tdp = NULL;
}

void thread_setup_kstack(thread_t *td, uintptr_t base, size_t size) {
  ASSERT(td->state == TDS_EMPTY);
  ASSERT(is_aligned(base, PAGE_SIZE));
  ASSERT(size > 0 && is_aligned(size, PAGE_SIZE));
  if (base == 0) {
    // allocate a new kernel stack
    td->kstack_base = vmap_anon(SIZE_2MB, 0, size, VM_RDWR|VM_STACK, "thread_kstack");
    td->kstack_size = size;
  } else {
    // use base as the kernel stack
    td->kstack_base = base;
    td->kstack_size = size;
  }
}

void thread_setup_priority(thread_t *td, uint8_t base_pri) {
  ASSERT(td->state == TDS_EMPTY);
  td->pri_base = base_pri;
  td->priority = base_pri;
}

//
// MARK: cpuset
//

#define CPUSET_MAX_INDEX (MAX_CPUS / 64)
#define cpuset_index(cpu) ((cpu) / 64)
#define cpuset_offset(cpu) ((cpu) % 64)
#define cpuset_cpu(index, offset) ((index) * 64 + (offset))

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

void cpuset_free(struct cpuset *set) {
  kfree(set);
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
    uint8_t index = __bsf64(bits & mask);
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

  if (td->crit_level == 0) {
    // first time entering a critical section, save flags
    PERCPU_SET_RFLAGS(rflags);
    td->crit_level++;
  }
}

void critical_exit() {
  thread_t *td = curthread;
  if (__expect_false(td == NULL)) {
    uint64_t flags = PERCPU_RFLAGS;
    temp_irq_restore(flags);
    return;
  }

  if (td->crit_level == 1) {
    // last time exiting a critical section, restore flags
    uint64_t flags = PERCPU_RFLAGS;
    temp_irq_restore(flags);
    td->crit_level--;
  }
}
