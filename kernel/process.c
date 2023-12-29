//
// Created by Aaron Gill-Braun on 2020-10-19.
//

#include <kernel/process.h>

#include <kernel/mm.h>
#include <kernel/fs.h>
#include <kernel/sched.h>
#include <kernel/thread.h>
#include <kernel/loader.h>
#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/panic.h>

#include <kernel/cpu/cpu.h>
#include <kernel/debug/debug.h>
#include <kernel/vfs/file.h>

#include <asm/bits.h>
#include <bitmap.h>

// #define ASSERT(x)
#define ASSERT(x) kassert(x)
// #define DPRINTF(...)
#define DPRINTF(x, ...) kprintf("proc: " x, ##__VA_ARGS__)

// MARK: Globals


// MARK: Process table
static struct ptable {
  bitmap_t *pidset;   // allocatable pid set
  process_t **array;  // array of process pointers
  size_t nprocs;
  mutex_t lock;
} ptable;

static void ptable_init() {
  size_t ptable_size = align(MAX_PROCS*sizeof(void *), PAGE_SIZE);
  uintptr_t ptable_base = vmap_anon(0, 0, ptable_size, VM_WRITE|VM_GLOBAL, "ptable");
  kassert(ptable_base != 0);

  ptable.pidset = create_bitmap(MAX_PROCS);
  ptable.array = (void *) ptable_base;
  ptable.nprocs = 0;
}
STATIC_INIT(ptable_init);

//
// MARK: creds

struct creds *creds_alloc() __move {
  struct creds *creds = kmallocz(sizeof(struct creds));
  creds->uid = 0;
  creds->gid = 0;
  creds->euid = 0;
  creds->egid = 0;
  return newref(creds);
}

//
// MARK: cpuset

#define CPUSET_MAX_INDEX (MAX_CPUS / 64)
#define cpuset_index(cpu) ((cpu) / 64)
#define cpuset_offset(cpu) ((cpu) % 64)
#define cpuset_cpu(index, offset) ((index) * 64 + (offset))

struct cpuset {
  uint64_t bits[MAX_CPUS / 64];
  size_t ncpus;
};

struct cpuset *cpuset_copy(struct cpuset *set) {
  struct cpuset *copy = kmallocz(sizeof(struct cpuset));
  memcpy(copy->bits, set->bits, sizeof(copy->bits));
  copy->ncpus = set->ncpus;
  return copy;
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
// MARK: Session
//

session_t *session_alloc_struct(pid_t sid) __move {
  session_t *session = kmallocz(sizeof(session_t));
  session->sid = sid;
  return newref(session);
}

void session_free_struct(session_t *session) {
  ASSERT(read_refcount(session) == 0);
  kfree(session);
}


//
// MARK: Process Group
//


//
// MARK: Thread
//

static inline int td_flags_to_tcb_flags(uint32_t td_flags) {
  int tcb_flags = 0;
  if (!(td_flags & TD_KTHREAD)) {
    // user thread
    tcb_flags |= TCB_FPU;
    tcb_flags |= TCB_IRETQ;
  }
  return tcb_flags;
}

static inline uint32_t td_flags_to_vm_flags(uint32_t td_flags) {
  uint32_t vm_flags = 0;
  if (!(td_flags & TD_KTHREAD)) {
    // user thread
    vm_flags |= VM_USER;
  }
  return vm_flags;
}

//

thread_t *thread_alloc_struct(process_t *proc, pid_t tid, uint32_t flags) {
  // p-lock must be held
  thread_t *td = kmallocz(sizeof(thread_t));
  td->tid = tid;
  td->flags = flags;
  td->process = proc;
  td->creds = getref(proc->creds);
  td->cpuset = kmallocz(sizeof(struct cpuset));

  td->tcb = tcb_alloc(td_flags_to_tcb_flags(flags));
  td->state = TDS_READY;
  td->cpu_id = PERCPU_ID;

  // allocate stack
  if (flags & TD_KTHREAD) {
    // kernel threads can be given
  }

  return td;
}

void thread_free_struct(thread_t *td) {
  putref(&td->creds, kfree);
  // putref(&td->process, )

  // putref()
  kfree(td);
}

//
// MARK: Process
//

void proc_init() {
  pgroup_t *pgroup = kmallocz(sizeof(pgroup_t));

  process_t *proc = kmallocz(sizeof(process_t));
  proc->space = PERCPU_ADDRESS_SPACE;
  proc->creds = creds_alloc();
  proc->pwd = fs_root_getref();
  proc->files = ftable_alloc();
  proc->state = PRS_ACTIVE;

  mutex_init(&proc->lock, 0);
  initref(proc);

  thread_t *main = thread_alloc_struct(proc, 0, TD_KTHREAD);
  proc->main = main;
  LIST_ADD(&proc->threads, proc->main, list);
  proc->nthreads = 1;

  PERCPU_SET_THREAD(main);
  PERCPU_SET_PROCESS(proc);
}


//
//

// static pid_t alloc_pid() {
//   mutex_lock()
// }

// pid_t alloc_pid() {
//   if (pid_nums == NULL) {
//     pid_nums = create_bitmap(MAX_PROCS);
//   }
//
//   spin_lock(&pid_nums_lock);
//   index_t pid = bitmap_get_set_free(pid_nums);
//   spin_unlock(&pid_nums_lock);
//   kassert(pid >= 0);
//   return (pid_t) pid;
// }

//

// process_t *process_alloc(pid_t pid, pid_t ppid, void *(start_routine)(void *), void *arg, str_t name) {
//   process_t *process = kmallocz(sizeof(process_t));
//   process->pid = pid;
//   process->ppid = ppid;
//   process->vm_space = PERCPU_ADDRESS_SPACE;
//   if (PERCPU_PROCESS != NULL) {
//     process->uid = PERCPU_PROCESS->uid;
//     process->gid = PERCPU_PROCESS->gid;
//     process->euid = process->uid;
//     process->egid = process->gid;
//   }
//   process->pwd = fs_root_getref();
//   process->files = ftable_alloc();
//   spin_init(&process->lock);
//
//   thread_t *main = thread_alloc(0, start_routine, arg, name, false);
//   main->process = process;
//   process->main = main;
//
//   LIST_INIT(&process->threads);
//   LIST_ADD_FRONT(&process->threads, main, group);
//   return process;
// }
//
// void process_free(process_t *process) {
//   unimplemented("process_free");
// }
//
// //
//
// void process_create_root(void (function)()) {
//   pid_t pid = alloc_pid();
//   process_t *process = process_alloc(pid, -1, root_process_wrapper, function, str_make("root"));
//
//   // allocate process table
//   ptable = kmallocz(sizeof(process_t *) * MAX_PROCS);
//   ptable[0] = process;
//   ptable_size = 1;
// }
//
// pid_t process_create(void (start_routine)(), str_t name) {
//   return process_create_1(start_routine, NULL, name);
// }
//
// pid_t process_create_1(void (start_routine)(), void *arg, str_t name) {
//   process_t *parent = PERCPU_PROCESS;
//   pid_t pid = alloc_pid();
//   proc_trace_debug("creating process %d | parent %d", pid, parent->pid);
//   process_t *process = process_alloc(pid, parent->pid, (void *) start_routine, arg, name);
//   ptable[process->pid] = process;
//   atomic_fetch_add(&ptable_size, 1);
//
//   sched_add(process->main);
//   return process->pid;
// }
//
// pid_t process_fork() {
//   kprintf("[process] creating process\n");
//   process_t *parent = PERCPU_PROCESS;
//   thread_t *parent_thread = PERCPU_THREAD;
//
//   process_t *process = kmalloc(sizeof(process_t));
//   process->pid = alloc_pid();
//   process->ppid = parent->pid;
//   process->vm_space = fork_address_space();
//   process->pwd = parent->pwd;
//   process->files = ftable_alloc(); // TODO: copy file table
//   kprintf("process: warning - file table not copied\n");
//
//   // clone main thread
//   thread_t *main = thread_copy(parent_thread);
//
//   uintptr_t stack = main->kernel_stack->address;
//   vm_mapping_t *vm = vm_get_mapping(stack);
//   kassert(vm != NULL);
//
//   uintptr_t frame = (uintptr_t) __builtin_frame_address(0);
//   uintptr_t offset = frame - (stack + vm->size);
//
//   uintptr_t rsp = stack + vm->size - offset;
//   main->ctx->rsp = rsp;
//   kprintf("[process] rsp: %p\n", rsp);
//
//   process->main = main;
//   LIST_ADD_FRONT(&process->threads, main, list);
//
//   ptable[process->pid] = process;
//   atomic_fetch_add(&ptable_size, 1);
//
//   sched_add(main);
//   return process->pid;
// }
//
// int process_execve(const char *path, char *const argv[], char *const envp[]) {
//   int res;
//   program_t prog = {0};
//   if ((res = load_executable(path, argv, envp, &prog)) < 0) {
//     kprintf("error: failed to load executable {:err}\n", res);
//     return res;
//   }
//
//   thread_t *thread = PERCPU_THREAD;
//   kassert(thread->user_stack == NULL);
//
//   thread->user_stack = prog.stack;
//   thread->user_sp = prog.sp;
//
//   vm_print_address_space();
//
//   // allocate a virtual region for the processes data segment.
//   // the size of this region (0 initially) determines the brk.
//   size_t rsvd = SIZE_16GB; // this is the max brk
//   vm_mapping_t *brk_vm = vmap_anon(rsvd, prog.end, 0, VM_WRITE | VM_USER | VM_FIXED, "data");
//   if (brk_vm == NULL) {
//     panic("error: failed to allocate data segment");
//   }
//   PERCPU_PROCESS->brk_vm = brk_vm;
//
//   sysret((uintptr_t) prog.entry, (uintptr_t) prog.sp);
// }
//
// pid_t process_getpid() {
//   return PERCPU_PROCESS->pid;
// }
//
// pid_t process_getppid() {
//   return PERCPU_PROCESS->ppid;
// }
//
// pid_t process_gettid() {
//   return PERCPU_THREAD->tid;
// }
//
// uid_t process_getuid() {
//   return PERCPU_PROCESS->uid;
// }
//
// gid_t process_getgid() {
//   return PERCPU_PROCESS->gid;
// }
//
// process_t *process_get(pid_t pid) {
//   if (ptable == NULL) {
//     return NULL;
//   }
//
//   if (pid >= MAX_PROCS || pid < 0) {
//     return NULL;
//   }
//   return ptable[pid];
// }
//
// //
// //
//
// void print_debug_process(process_t *process) {
//   kprintf("process:\n");
//   kprintf("  pid: %d\n", process->pid);
//   kprintf("  ppid: %d\n", process->ppid);
//
//   thread_t *thread = LIST_FIRST(&process->threads);
//   while (thread) {
//     print_debug_thread(thread);
//     thread = LIST_NEXT(thread, list);
//   }
// }
//
// void proc_print_thread_stats(process_t *proc) {
//   kassert(proc != NULL);
//
//   kprintf("proc: process %d(%d) | CPU#%d\n", proc->pid, proc->ppid, PERCPU_ID);
//   thread_t *thread;
//   LIST_FOREACH(thread, &proc->threads, group) {
//     sched_stats_t *stats = thread->stats;
//     kprintf("\t--> thread %d\n", thread->tid);
//     kprintf("\t\ttotal_time=%llu, sched_count=%zu, preempt_count=%zu, sleep_count=%zu, yield_count=%zu\n",
//             stats->total_time, stats->sched_count, stats->preempt_count, stats->sleep_count, stats->yield_count);
//   }
// }
//
// // MARK: Syscalls
//
// DEFINE_SYSCALL(brk, void *, void *addr) {
//   vm_mapping_t *vm = PERCPU_PROCESS->brk_vm;
//   uintptr_t orig_brk = vm->address + vm->size;
//   uintptr_t new_brk = align((uintptr_t)addr, PAGE_SIZE);
//   if (!vm_mapping_contains(vm, new_brk)) {
//     return (void *) orig_brk;
//   }
//
//   size_t new_size = new_brk - vm->address;
//   if (vm_resize(vm, new_size, false) < 0) {
//     return (void *) orig_brk;
//   }
//
//   return (void *) new_brk;
// }
//
// SYSCALL_ALIAS(getpid, process_getpid);
// SYSCALL_ALIAS(getuid, process_getuid);
// SYSCALL_ALIAS(getgid, process_getgid);
// SYSCALL_ALIAS(gettid, process_gettid);
// SYSCALL_ALIAS(getppid, process_getppid);
//
// DEFINE_SYSCALL(setuid, int, uid_t uid) {
//   PERCPU_PROCESS->euid = uid;
//   return 0;
// }
//
// DEFINE_SYSCALL(setgid, int, gid_t gid) {
//   PERCPU_PROCESS->egid = gid;
//   return 0;
// }
//
// DEFINE_SYSCALL(geteuid, uid_t) {
//   return PERCPU_PROCESS->euid;
// }
//
// DEFINE_SYSCALL(getegid, gid_t) {
//   return PERCPU_PROCESS->egid;
// }


//
//

void spinlock_enter() {
  thread_t *td = PERCPU_THREAD;
  if (td == NULL) {
    uint64_t rflags;
    temp_irq_save(rflags);
    PERCPU_SET_RFLAGS(rflags);
    return;
  }

  if (td->spinlock_count == 0) {
    // first time entering a spinlock, time for this thread to enter a critical section
    uint64_t rflags;
    temp_irq_save(rflags);
    td->saved_rflags = rflags;
    td->spinlock_count = 1;
    // td->critical_level
    // td_begin_critical(td);
  }
}

void spinlock_exit() {
  thread_t *td = PERCPU_THREAD;
  if (td == NULL) {
    uint64_t flags = PERCPU_RFLAGS;
    temp_irq_restore(flags);
    return;
  }
}
