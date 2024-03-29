//
// Created by Aaron Gill-Braun on 2021-03-18.
//

#include <kernel/thread.h>

#include <kernel/mm.h>
#include <kernel/mutex.h>
#include <kernel/sched.h>

#include <kernel/printf.h>
#include <kernel/string.h>
#include <kernel/panic.h>
#include <kernel/process.h>
#include <atomic.h>

#include <kernel/debug/debug.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(x, ...) kprintf(x, ##__VA_ARGS__)

// #define THREAD_DEBUG
#ifdef THREAD_DEBUG
#define thread_trace_debug(str, args...) kprintf("[thread] " str "\n", ##args)
#else
#define thread_trace_debug(str, args...)
#endif


extern void thread_entry_stub();

static inline const char *get_status_str(thread_status_t status) {
  switch (status) {
    case THREAD_READY:
      return "READY";
    case THREAD_RUNNING:
      return "RUNNING";
    case THREAD_BLOCKED:
      return "BLOCKED";
    case THREAD_SLEEPING:
      return "SLEEPING";
    case THREAD_TERMINATED:
      return "TERMINATED";
    case THREAD_KILLED:
      return "KILLED";
    default:
      return "";
  }
}

static inline vm_mapping_t *create_stack(uintptr_t *sp, bool user) {
  kassert(sp != NULL);
  size_t size = user ? USER_STACK_SIZE : KERNEL_STACK_SIZE;
  uint32_t vm_flags = VM_WRITE | VM_STACK | (user ? VM_USER : 0);

  page_t *stack_pages = alloc_pages(SIZE_TO_PAGES(size));
  vm_mapping_t *vm = vmap_pages(stack_pages, 0, size, vm_flags, "thread stack");
  uintptr_t base = vm->address;
  uintptr_t rsp = vm->address + size;
  *sp = rsp;
  return vm;
}

//

__used void *thread_entry(void *(start_routine)(void *), void *arg) {
  void *result = start_routine(arg);
  thread_exit((int)(uintptr_t) result);
  return NULL;
}

//
// Thread Allocation
//

thread_t *thread_alloc(id_t tid, void *(start_routine)(void *), void *arg, str_t name, bool user) {
  thread_t *thread = kmalloc(sizeof(thread_t));
  memset(thread, 0, sizeof(thread_t));

  // create kernel stack
  uintptr_t kernel_sp = 0;
  vm_mapping_t *kernel_stack = create_stack(&kernel_sp, false);
  uintptr_t user_sp = 0;
  vm_mapping_t *user_stack = NULL;
  if (user) {
    user_stack = create_stack(&user_sp, true);
  }

  ((uint64_t *) kernel_sp)[-1] = (uintptr_t) start_routine;
  ((uint64_t *) kernel_sp)[-2] = (uintptr_t) arg;
  kernel_sp -= (2 * sizeof(uintptr_t));

  // create thread context
  thread_ctx_t *ctx = kmalloc(sizeof(thread_ctx_t));
  memset(ctx, 0, sizeof(thread_ctx_t));
  ctx->rip = (uintptr_t) thread_entry_stub;
  ctx->cs = KERNEL_CS;
  ctx->rflags = DEFAULT_RFLAGS;
  ctx->rsp = kernel_sp;

  sched_stats_t *stats = kmallocz(sizeof(sched_stats_t));

  thread->tid = tid;
  thread->ctx = ctx;
  thread->fs_base = 0;
  thread->flags = F_THREAD_CREATED;
  thread->kernel_sp = kernel_sp;
  thread->user_sp = user_sp;
  thread->status = THREAD_READY;
  thread->cpu_id = PERCPU_ID;
  thread->policy = POLICY_SYSTEM;
  thread->stats = stats;
  thread->affinity = -1;
  thread->name = name;

  thread->kernel_stack = kernel_stack;
  thread->user_stack = user_stack;

  spin_init(&thread->lock);
  mutex_init(&thread->mutex, MUTEX_LOCKED);
  return thread;
}

thread_t *thread_copy(thread_t *other) {
  bool user = other->user_stack != NULL;
  thread_t *thread = thread_alloc(0, NULL, NULL, str_dup(other->name), user);

  // copy the stacks
  memcpy((void *) thread->kernel_stack->address, (void *) other->kernel_stack->address, KERNEL_STACK_SIZE);
  if (user) {
    memcpy((void *) thread->user_stack->address, (void *) other->user_stack->address, USER_STACK_SIZE);
  }

  uintptr_t kernel_sp_rel = other->kernel_sp - other->kernel_stack->address;
  uintptr_t user_sp_rel;
  if (user) {
    user_sp_rel = other->user_sp - other->user_stack->address;
  }

  other->fs_base = thread->fs_base;
  // TODO: does this need to be copied?

  thread->kernel_sp = thread->kernel_stack->address + kernel_sp_rel;
  thread->user_sp = user ? thread->user_stack->address + user_sp_rel : 0;
  thread->cpu_id = PERCPU_ID;
  thread->policy = other->policy;
  thread->priority = other->priority;
  thread->affinity = other->affinity;
  thread->errno = other->errno;
  thread->status = other->status;
  thread->preempt_count = 0; // what do we do here?

  return thread;
}

void thread_free(thread_t *thread) {
  unimplemented("thread free");
}

//
// Thread API
//

thread_t *thread_create(void *(start_routine)(void *), void *arg, str_t name) {
  process_t *process = PERCPU_PROCESS;
  id_t tid = atomic_fetch_add(&process->num_threads, 1);
  thread_trace_debug("creating thread %d | process %d", tid, process->pid);

  thread_t *thread = thread_alloc(tid, start_routine, arg, name, false);
  thread->process = process;
  thread->affinity = process->main->affinity;

  if (str_isnull(name)) {
    // in lieu of an explicit thread name we can try and get the name of
    // the start routine and use that instead
    const char *func_name = debug_function_name((uintptr_t) start_routine);
    if (func_name != NULL) {
      // duplicate it because debug_function_name returns a non-owning string
      thread->name = str_make(func_name);
    }
  }

  PROCESS_LOCK(process);
  LIST_ADD(&process->threads, thread, group);
  PROCESS_UNLOCK(process);
  sched_add(thread);
  return thread;
}

noreturn void thread_exit(int retval) {
  kprintf("[thread] thread_exit(%d)\n", retval);
  thread_t *thread = PERCPU_THREAD;
  thread_trace_debug("thread %d process %d exiting", thread->tid, thread->process->pid);
  thread->errno = retval;
  sched_terminate(thread);
  unreachable;
}

void thread_sleep(uint64_t us) {
  thread_trace_debug("thread %d process %d sleeping for %lf seconds", process_gettid(),
                     process_getpid(), (double)(us) / 1e6);
  sched_sleep(US_TO_NS(us));
  thread_trace_debug("thread %d process %d wakeup", process_gettid(), process_getpid());
}

void thread_yield() {
  thread_trace_debug("thread %d process %d yielded", process_gettid(), process_getpid());
  sched_yield();
}

void thread_block() {
  thread_trace_debug("thread %d process %d blocked", process_gettid(), process_getpid());
  sched_block(PERCPU_THREAD);
}

//

int thread_setpolicy(uint8_t policy) {
  sched_opts_t opts = {
    .policy = policy,
    .priority = PERCPU_THREAD->priority,
    .affinity = PERCPU_THREAD->affinity,
  };
  return sched_setsched(opts);
}

int thread_setpriority(uint16_t priority) {
  sched_opts_t opts = {
    .policy = PERCPU_THREAD->policy,
    .priority = priority,
    .affinity = PERCPU_THREAD->affinity,
  };
  return sched_setsched(opts);
}

int thread_setaffinity(uint8_t affinity) {
  sched_opts_t opts = {
    .policy = PERCPU_THREAD->policy,
    .priority = PERCPU_THREAD->priority,
    .affinity = affinity,
  };
  return sched_setsched(opts);
}

int thread_setsched(uint8_t policy, uint16_t priority) {
  sched_opts_t opts = {
    .policy = policy,
    .priority = priority,
    .affinity = PERCPU_THREAD->affinity,
  };
  return sched_setsched(opts);
}

void preempt_disable() {
  PERCPU_THREAD->preempt_count++;
}

void preempt_enable() {
  PERCPU_THREAD->preempt_count--;
}

void print_debug_thread(thread_t *thread) {
  // uint8_t cpu_id;         // current/last cpu used
  // uint8_t policy;         // thread scheduling policy
  // uint16_t priority;      // thread priority
  // thread_status_t status; // thread status
  //
  // uint32_t signal;        // signal mask
  // uint32_t flags;         // flags mask
  //
  // int _errno;             // thread local errno
  kprintf("thread %d:\n"
          "  cpu: %d\n"
          "  policy: %d\n"
          "  priority: %d\n"
          "  status: %s\n"
          "  \n"
          "  flags: %b\n"
          "  \n"
          "  errno: %d\n",
          thread->tid, thread->cpu_id, thread->policy, thread->priority,
          get_status_str(thread->status), thread->flags, thread->errno);
}

// MARK: Syscalls

SYSCALL_ALIAS(exit, thread_exit);

DEFINE_SYSCALL(exit_group, noreturn void, int status) {
  DPRINTF("exit_group(%d)\n", status);
  thread_exit(status);
}

DEFINE_SYSCALL(set_tid_address, pid_t, int *tidptr) {
  return PERCPU_THREAD->tid;
}

