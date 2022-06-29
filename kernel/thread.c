//
// Created by Aaron Gill-Braun on 2021-03-18.
//

#include <thread.h>

#include <mm.h>
#include <mutex.h>
#include <scheduler.h>

#include <printf.h>
#include <string.h>
#include <panic.h>
#include <process.h>
#include <signal.h>

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

static inline page_t *create_stack(uintptr_t *sp, bool user) {
  kassert(sp != NULL);
  size_t virt_size = user ? USER_VSTACK_SIZE : KERNEL_STACK_SIZE;
  size_t phys_size = user ? USER_PSTACK_SIZE : KERNEL_STACK_SIZE;

  page_t *stack_pages = _alloc_pages(SIZE_TO_PAGES(phys_size), (user ? PG_USER : 0) | PG_WRITE);
  void *va = _vmap_pages(stack_pages);

  uintptr_t rsp = (uintptr_t) va + virt_size;
  *sp = rsp;
  return stack_pages;
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
  page_t *kernel_stack = create_stack(&kernel_sp, false);
  uintptr_t user_sp = 0;
  page_t *user_stack = NULL;
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
  thread_meta_ctx_t *mctx = kmalloc(sizeof(thread_meta_ctx_t));
  memset(mctx, 0, sizeof(thread_meta_ctx_t));

  // create thread local storage block but don't
  // allocate space for tls data until its requested
  tls_block_t *tls = kmalloc(sizeof(tls_block_t));
  tls->addr = 0;
  tls->size = 0;
  tls->pages = NULL;

  thread->tid = tid;
  thread->ctx = ctx;
  thread->mctx = mctx;
  thread->tls = tls;
  thread->kernel_sp = kernel_sp;
  thread->user_sp = user_sp;
  thread->status = THREAD_READY;
  thread->policy = SCHED_SYSTEM;

  thread->kernel_stack = kernel_stack;
  thread->user_stack = user_stack;

  mutex_init(&thread->mutex, MUTEX_LOCKED);
  cond_init(&thread->data_ready, 0);
  return thread;
}

thread_t *thread_copy(thread_t *other) {
  bool user = other->user_stack != NULL;
  thread_t *thread = thread_alloc(0, NULL, NULL, user);

  // copy the stacks
  memcpy((void *) PAGE_VIRT_ADDR(thread->kernel_stack), (void *) PAGE_VIRT_ADDR(other->kernel_stack), KERNEL_STACK_SIZE);
  if (user) {
    memcpy((void *) PAGE_VIRT_ADDR(thread->user_stack), (void *) PAGE_VIRT_ADDR(other->user_stack), USER_PSTACK_SIZE);
  }

  uintptr_t kernel_sp_rel = other->kernel_sp - PAGE_VIRT_ADDR(other->kernel_stack);
  uintptr_t user_sp_rel;
  if (user) {
    user_sp_rel = other->user_sp - PAGE_VIRT_ADDR(other->user_stack);
  }

  // copy the context
  memcpy(thread->ctx, other->ctx, sizeof(thread_ctx_t));
  // copy the meta context
  memcpy(thread->mctx, other->mctx, sizeof(thread_meta_ctx_t));
  // copy the tls block
  memcpy(thread->tls, other->tls, sizeof(tls_block_t));

  // copy the tls data (if needed)
  // to do

  thread->kernel_sp = PAGE_VIRT_ADDR(thread->kernel_stack) + kernel_sp_rel;
  thread->user_sp = user ? PAGE_VIRT_ADDR(thread->user_stack) + user_sp_rel : 0;
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
    vfree_pages(thread->kernel_stack);
  }
  if (thread->user_stack) {
    vfree_pages(thread->user_stack);
  }
  if (thread->tls) {
    tls_block_t *tls = thread->tls;
    if (tls->pages) {
      vfree_pages(tls->pages);
    }
    kfree(tls);
  }
  kfree(thread->ctx);
  kfree(thread);
}

//
// Thread API
//

thread_t *thread_create(void *(start_routine)(void *), void *arg) {
  process_t *process = PERCPU_PROCESS;
  id_t tid = LIST_LAST(&process->threads)->tid + 1;
  thread_trace_debug("creating thread %d | process %d", tid, process->pid);

  thread_t *thread = thread_alloc(tid, start_routine, arg, false);
  thread->process = process;

  LIST_ADD(&process->threads, thread, group);
  scheduler_add(thread);
  return thread;
}

void thread_exit(void *retval) {
  thread_t *thread = PERCPU_THREAD;
  thread_trace_debug("thread %d process %d exiting", thread->tid, thread->process->pid);
  thread->data = retval;
  mutex_unlock(&thread->mutex);
  scheduler_remove(thread);
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
  scheduler_sleep(us * 1000);
  thread_trace_debug("thread %d process %d wakeup", gettid(), getpid());
}

void thread_yield() {
  thread_trace_debug("thread %d process %d yielded", gettid(), getpid());
  scheduler_yield();
}

void thread_block() {
  thread_trace_debug("thread %d process %d blocked", gettid(), getpid());
  scheduler_block(PERCPU_THREAD);
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

int thread_setpolicy(thread_t *thread, uint8_t policy) {
  return scheduler_update(thread, policy, thread->priority);
}

int thread_setpriority(thread_t *thread, uint16_t priority) {
  return scheduler_update(thread, thread->policy, priority);
}

int thread_setsched(thread_t *thread, uint8_t policy, uint16_t priority) {
  return scheduler_update(thread, policy, priority);
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
