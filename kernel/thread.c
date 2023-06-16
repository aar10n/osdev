//
// Created by Aaron Gill-Braun on 2021-03-18.
//

#include <thread.h>

#include <mm.h>
#include <mutex.h>
#include <sched.h>

#include <printf.h>
#include <string.h>
#include <panic.h>
#include <process.h>
#include <signal.h>
#include <atomic.h>

#include <debug/debug.h>

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
  uint32_t pg_flags = PG_WRITE | (user ? PG_USER : 0);
  uint32_t vm_flags = VM_STACK | VM_GUARD | (user ? VM_USER : 0);

  page_t *stack_pages = alloc_pages(SIZE_TO_PAGES(size), pg_flags);
  vm_mapping_t *vm = vm_alloc_pages(stack_pages, 0, size, vm_flags, "thread stack");
  uintptr_t base = (uintptr_t) vm_map(vm, pg_flags);
  uintptr_t rsp = vm->address + size;
  *sp = rsp;
  return vm;
}

//

__used void *thread_entry(void *(start_routine)(void *), void *arg) {
  void *result = start_routine(arg);
  thread_exit(result);
  return NULL;
}

//
// Thread Allocation
//

thread_t *thread_alloc(id_t tid, void *(start_routine)(void *), void *arg, bool user) {
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

  // create thread meta context
  thread_meta_ctx_t *mctx = kmallocz(sizeof(thread_meta_ctx_t));
  memset(mctx, 0, sizeof(thread_meta_ctx_t));

  sched_stats_t *stats = kmallocz(sizeof(sched_stats_t));

  thread->tid = tid;
  thread->ctx = ctx;
  thread->mctx = mctx;
  thread->fs_base = 0;
  thread->flags = F_THREAD_CREATED;
  thread->kernel_sp = kernel_sp;
  thread->user_sp = user_sp;
  thread->status = THREAD_READY;
  thread->cpu_id = PERCPU_ID;
  thread->policy = POLICY_SYSTEM;
  thread->stats = stats;
  thread->affinity = -1;
  thread->name = NULL;

  thread->kernel_stack = kernel_stack;
  thread->user_stack = user_stack;

  spin_init(&thread->lock);
  mutex_init(&thread->mutex, MUTEX_LOCKED);
  cond_init(&thread->data_ready, 0);
  return thread;
}

thread_t *thread_copy(thread_t *other) {
  char *name = other->name ? kasprintf("%s (copy)", other->name) : NULL;
  bool user = other->user_stack != NULL;
  thread_t *thread = thread_alloc(0, NULL, NULL, user);
  thread->name = name;

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
  thread->status = other->status;

  thread->errno = other->errno;
  thread->preempt_count = 0; // what do we do here?

  return thread;
}

void thread_free(thread_t *thread) {
  if (thread->kernel_stack) {
    vm_free(thread->kernel_stack);
  }
  if (thread->user_stack) {
    vm_free(thread->user_stack);
  }

  kfree(thread->name);
  kfree(thread->ctx);
  kfree(thread);
}

//
// Thread API
//

thread_t *thread_create(void *(start_routine)(void *), void *arg) {
  process_t *process = PERCPU_PROCESS;
  id_t tid = atomic_fetch_add(&process->num_threads, 1);
  thread_trace_debug("creating thread %d | process %d", tid, process->pid);

  thread_t *thread = thread_alloc(tid, start_routine, arg, false);
  thread->process = process;
  thread->affinity = process->main->affinity;

  // in lieu of an explicit thread name we can try and get the name of
  // the start routine and use that instead
  const char *func_name = debug_function_name((uintptr_t) start_routine);
  if (func_name != NULL) {
    // duplicate it because debug_function_name returns a non-owning string
    thread->name = strdup(func_name);
  }

  PROCESS_LOCK(process);
  LIST_ADD(&process->threads, thread, group);
  PROCESS_UNLOCK(process);
  sched_add(thread);
  return thread;
}

thread_t *thread_create_n(char *name, void *(start_routine)(void *), void *arg) {
  process_t *process = PERCPU_PROCESS;
  id_t tid = atomic_fetch_add(&process->num_threads, 1);
  thread_trace_debug("creating thread %d | process %d", tid, process->pid);

  thread_t *thread = thread_alloc(tid, start_routine, arg, false);
  thread->process = process;
  thread->affinity = process->main->affinity;
  thread->name = name;

  PROCESS_LOCK(process);
  LIST_ADD(&process->threads, thread, group);
  PROCESS_UNLOCK(process);
  sched_add(thread);
  return thread;
}

void thread_exit(void *retval) {
  thread_t *thread = PERCPU_THREAD;
  thread_trace_debug("thread %d process %d exiting", thread->tid, thread->process->pid);
  thread->data = retval;
  mutex_unlock(&thread->mutex);
  sched_terminate(thread);
}

int thread_join(thread_t *thread, void **retval) {
  thread_t *curr = PERCPU_THREAD;
  if (thread->process != curr->process || thread == curr || thread->flags & F_THREAD_JOINING) {
    return EINVAL;
  }

  thread->flags |= F_THREAD_JOINING;
  int result = mutex_lock(&thread->mutex);
  if (result != 0) {
    return result;
  }

  if (retval != NULL) {
    *retval = thread->data;
  }
  // free thread
  panic("not implemented");
  return 0;
}

int thread_send(void *data) {
  // send data to awaiting thread
  thread_t *curr = PERCPU_THREAD;
  curr->data = data;
  cond_signal(&curr->data_ready);
  return 0;
}

int thread_receive(thread_t *thread, void **data) {
  // wait for data to be sent from thread
  thread_t *curr = PERCPU_THREAD;
  if (thread->process != curr->process || thread == curr) {
    return EINVAL;
  }

  cond_wait(&thread->data_ready);
  if (data != NULL) {
    *data = thread->data;
  }
  return 0;
}

void thread_sleep(uint64_t us) {
  thread_trace_debug("thread %d process %d sleeping for %lf seconds", gettid(), getpid(), (double)(us) / 1e6);
  sched_sleep(US_TO_NS(us));
  thread_trace_debug("thread %d process %d wakeup", gettid(), getpid());
}

void thread_yield() {
  thread_trace_debug("thread %d process %d yielded", gettid(), getpid());
  sched_yield();
}

void thread_block() {
  thread_trace_debug("thread %d process %d blocked", gettid(), getpid());
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

//

int thread_alloc_stack(thread_t *thread, bool user) {
  kassert(user == true);
  kassert(thread->user_stack == NULL);

  uintptr_t user_sp = 0;
  page_t *stack = create_stack(&user_sp, user);
  thread->user_stack = stack;
  thread->user_sp = user_sp;
  return 0;
}

//

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
          "  signal: %b\n"
          "  flags: %b\n"
          "  \n"
          "  errno: %d\n",
          thread->tid, thread->cpu_id, thread->policy, thread->priority,
          get_status_str(thread->status), thread->signal, thread->flags, thread->errno);
}
