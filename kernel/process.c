//
// Created by Aaron Gill-Braun on 2020-10-19.
//

#include <process.h>
#include <scheduler.h>
#include <string.h>
#include <atomic.h>

#include <mm/heap.h>
#include <mm/mm.h>
#include <mm/vm.h>

#include <fs.h>
#include <panic.h>

#include <stdio.h>

static uint64_t __pid = 0;

uint64_t alloc_pid() {
  return atomic_fetch_add(&__pid, 1);
}

process_t *alloc_process(pid_t ppid, pid_t pid) {
  process_t *process = kmalloc(sizeof(process_t));
  memset(process, 0, sizeof(process_t));

  process->pid = pid;
  process->ppid = ppid;
  process->cpu_id = ID;
  process->policy = 0;
  process->priority = 0;
  process->status = PROC_READY;
  process->pwd = NULL;
  process->vm = NULL;
  process->files = NULL;

  context_t *ctx = kmalloc(sizeof(context_t));
  memset(ctx, 0, sizeof(context_t));
  ctx->cs = KERNEL_CS;
  ctx->rflags = DEFAULT_RFLAGS;
  process->ctx = ctx;

  return process;
}

process_t *kthread_create(void (*func)()) {
  kprintf("[process] creating kernel process\n");

  process_t *parent = current;
  process_t *process = alloc_process(parent ? parent->pid : -1, alloc_pid());
  process->pwd = parent ? parent->pwd : fs_root;
  process->vm = parent ? parent->vm : VM;
  if (parent && parent->files) {
    process->files = copy_file_table(parent->files);
  } else {
    process->files = create_file_table();
  }

  process->ctx->rip = (uintptr_t) func;

  // allocate a process stack
  page_t *page = alloc_pages(SIZE_TO_PAGES(PROC_STACK_SIZE), PE_WRITE);
  void *stack = vm_map_page_search(page, ABOVE, STACK_VA);
  process->ctx->rsp = (uintptr_t) stack + PROC_STACK_SIZE;

  kprintf("[process] stack at %p\n", process->ctx->rsp);
  return process;
}

process_t *kthread_create_idle(void (*func)()) {
  kprintf("[process] creating idle process\n");

  process_t *process = alloc_process(0, -1);
  process->vm = VM;
  process->ctx->rip = (uintptr_t) func;

  page_t *page = alloc_pages(1, PE_WRITE);
  void *stack = vm_map_page_search(page, ABOVE, STACK_VA);
  process->ctx->rsp = (uintptr_t) stack + PROC_STACK_SIZE;

  kprintf("[process] stack at %p\n", process->ctx->rsp);
  return process;
}

//

pid_t process_fork(bool user) {
  kprintf("[process] creating process\n");

  process_t *parent = current;
  if (user) {
    kassert(parent != NULL);
  }

  // allocate a process stack
  page_t *page = alloc_pages(SIZE_TO_PAGES(STACK_SIZE), user ? PE_USER : 0 | PE_WRITE);
  void *stack = vm_map_page_search(page, ABOVE, STACK_VA);

  memcpy(stack, (void *) STACK_VA - STACK_SIZE, STACK_SIZE);
  uintptr_t frame = (uintptr_t) __builtin_frame_address(0);
  uintptr_t offset = frame - STACK_VA;

  uintptr_t rsp = (uintptr_t) stack + STACK_SIZE - offset;
  // uintptr_t rsp = frame;
  kprintf("[process] rsp: %p\n", rsp);

  process_t *process = kmalloc(sizeof(process_t));
  process->pid = alloc_pid();
  process->ppid = parent ? parent->pid : 0;
  process->cpu_id = ID;
  process->policy = 0;
  process->priority = 1;
  process->status = PROC_READY;
  process->pwd = parent ? parent->pwd : fs_root;
  process->files = parent ? copy_file_table(parent->files) : create_file_table();
  process->vm = vm_duplicate();

  context_t *ctx = kmalloc(sizeof(context_t));
  memset(ctx, 0, sizeof(context_t));
  ctx->rip = (uintptr_t) __builtin_return_address(0);
  ctx->cs = user ? USER_CS : KERNEL_CS;
  ctx->rflags = DEFAULT_RFLAGS;
  ctx->ss = user ? USER_DS : 0;
  ctx->rax = process->pid;
  ctx->rsp = rsp;

  process->ctx = ctx;

  memset(&process->stats, 0, sizeof(process->stats));
  sched_enqueue(process);
  return process->pid;
}


pid_t getpid() {
  return current->pid;
}

pid_t getppid() {
  return current->ppid;
}

//

void print_debug_process(process_t *process) {
  kprintf("process:\n");
  kprintf("  pid: %d\n", process->pid);
  kprintf("  ppid: %d\n", process->ppid);
  kprintf("  cpu: %d\n", process->cpu_id);
  kprintf("  policy: %d\n", process->policy);
  kprintf("  priority: %d\n", process->priority);
  kprintf("  stats:\n");
  kprintf("    last run start: %llu\n", process->stats.last_run_start);
  kprintf("    last run end: %llu\n", process->stats.last_run_end);
  kprintf("    run time: %llu\n", process->stats.run_time);
  kprintf("    idle time: %llu\n", process->stats.idle_time);
  kprintf("    sleep time: %llu\n", process->stats.sleep_time);
  kprintf("    run count: %llu\n", process->stats.run_count);
  kprintf("    block count: %llu\n", process->stats.block_count);
  kprintf("    sleep count: %llu\n", process->stats.sleep_count);
  kprintf("    yield count: %llu\n", process->stats.yield_count);
}
