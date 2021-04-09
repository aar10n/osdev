//
// Created by Aaron Gill-Braun on 2021-03-18.
//

#include <thread.h>
#include <printf.h>
#include <string.h>
#include <panic.h>
#include <process.h>
#include <scheduler.h>
#include <atomic.h>
#include <mutex.h>
#include <mm.h>

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


id_t alloc_tid(id_t last_tid) {
  return atomic_fetch_add(&last_tid, 1);
}

void *thread_entry(void *(start_routine)(void *), void *arg) {
  void *result = start_routine(arg);
  thread_exit(result);
  return NULL;
}

//
// Thread Allocation
//

thread_t *thread_alloc(id_t tid, void *(start_routine)(void *), void *arg) {
  thread_t *thread = kmalloc(sizeof(thread_t));
  memset(thread, 0, sizeof(thread_t));

  // create thread stack
  size_t num_pages = SIZE_TO_PAGES(THREAD_STACK_SIZE);
  page_t *stack_pages = alloc_zero_pages(num_pages, PE_WRITE);
  uintptr_t *stack_top = (void *) stack_pages->addr + THREAD_STACK_SIZE;
  // make arguments accessible to entry stub
  stack_top[-1] = (uintptr_t) start_routine;
  stack_top[-2] = (uintptr_t) arg;

  // create thread context
  thread_ctx_t *ctx = kmalloc(sizeof(thread_ctx_t));
  memset(ctx, 0, sizeof(thread_ctx_t));
  ctx->rip = (uintptr_t) thread_entry_stub;
  ctx->cs = KERNEL_CS;
  ctx->rflags = DEFAULT_RFLAGS;
  ctx->rsp = ((uintptr_t) stack_top) - (2 * sizeof(uintptr_t));

  // create thread local storage block but don't
  // allocate space for tls data until its requested
  tls_block_t *tls = kmalloc(sizeof(tls_block_t));
  tls->addr = 0;
  tls->size = 0;
  tls->pages = NULL;

  thread->tid = tid;
  thread->ctx = ctx;
  thread->stack = stack_pages;
  thread->tls = tls;
  thread->status = THREAD_READY;
  thread->policy = SCHED_SYSTEM;
  mutex_init_locked(&thread->mutex, thread, 0);
  cond_init(&thread->data_ready, 0);
  return thread;
}

thread_t *thread_copy(thread_t *other) {
  thread_t *thread = thread_alloc(0, NULL, NULL);

  // copy the stack
  size_t stack_size = SIZE_TO_PAGES(THREAD_STACK_SIZE);
  memcpy((void *) thread->stack->addr, (void *) other->stack->addr, stack_size);

  // copy the context
  thread_ctx_t *ctx = kmalloc(sizeof(thread_ctx_t));
  memcpy(ctx, other->ctx, sizeof(thread_ctx_t));

  // copy the tls block
  tls_block_t *tls = kmalloc(sizeof(tls_block_t));
  memcpy(tls, other->tls, sizeof(tls_block_t));

  // copy the tls data (if needed)
  // to do

  thread->tid = 0;
  thread->ctx = ctx;
  thread->process = NULL;
  thread->tls = tls;

  thread->cpu_id = 0;
  thread->policy = other->policy;
  thread->priority = other->priority;
  thread->status = other->status;

  thread->_errno = other->_errno;
  thread->preempt_count = 0; // what do we do here?

  return thread;
}

void thread_free(thread_t *thread) {
  if (thread->stack) {
    vm_unmap_page(thread->stack);
    mm_free_page(thread->stack);
  }
  if (thread->tls) {
    tls_block_t *tls = thread->tls;
    if (tls->pages) {
      vm_unmap_page(tls->pages);
      mm_free_page(tls->pages);
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
  process_t *process = current_process;
  id_t tid = process->threads->tid + 1;
  thread_t *thread = thread_alloc(tid, start_routine, arg);
  thread->process = process;
  thread->g_next = process->threads;

  process->threads->g_prev = thread;
  process->threads = thread;

  scheduler_add(thread);
  return thread;
}

void thread_exit(void *retval) {
  thread_t *thread = current_thread;
  thread->data = retval;
  mutex_unlock(&thread->mutex);
  scheduler_remove(thread);
}

int thread_join(thread_t *thread, void **retval) {
  thread_t *curr = current_thread;
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
  thread_t *curr = current_thread;
  curr->data = data;
  cond_signal(&curr->data_ready);
  return 0;
}

int thread_receive(thread_t *thread, void **data) {
  // wait for data to be sent from thread
  thread_t *curr = current_thread;
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
  kprintf("[pid %d:%d] sleeping for %lf seconds\n", getpid(), gettid(), (double)(us) / 1e6);
  scheduler_sleep(us * 1000);
}

void thread_yield() {
  scheduler_yield();
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
          get_status_str(thread->status), thread->signal, thread->flags, thread->_errno);
}
