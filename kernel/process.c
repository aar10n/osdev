//
// Created by Aaron Gill-Braun on 2020-10-19.
//

#include <process.h>
#include <percpu.h>
#include <string.h>
#include <atomic.h>

#include <mm/heap.h>
#include <mm/mm.h>
#include <mm/vm.h>
#include <stdio.h>

static uint64_t __pid = 0;

uint64_t alloc_pid() {
  return atomic_fetch_add(&__pid, 1);
}

void process_init(process_t *process) {
  process->pid = alloc_pid();
  process->ctx = NULL;
  process->cpu_id = PERCPU->id;
  process->policy = 0;
  process->priority = process->pid;
  memset(&process->stats, 0, sizeof(process->stats));
}

process_t *create_process(void (*func)()) {
  kprintf("[process] creating process\n");
  // allocate a process stack
  page_t *page = alloc_page(PE_WRITE);
  void *stack = vm_map_page(page);
  memset(stack, 0, PAGE_SIZE);

  context_t *ctx = kmalloc(sizeof(context_t));
  memset(ctx, 0, sizeof(context_t));
  ctx->rip = (uintptr_t) func;
  ctx->cs = KERNEL_CS;
  ctx->rflags = DEFAULT_RFLAGS;
  ctx->rsp = (uintptr_t) stack + PAGE_SIZE - 1;
  ctx->ss = 0;

  kprintf("[process] stack at %p\n", ctx->rsp);
  process_t *process = kmalloc(sizeof(process_t));
  process_init(process);
  process->ctx = ctx;
  return process;
}

void kthread_create(void (*func)()) {
  kprintf("[process] creating thread\n");
  // allocate a process stack
  page_t *page = alloc_page(PE_WRITE);
  void *stack = vm_map_page(page);
  memset(stack, 0, PAGE_SIZE);

  context_t *ctx = kmalloc(sizeof(context_t));
  memset(ctx, 0, sizeof(context_t));
  ctx->rip = (uintptr_t) func;
  ctx->cs = KERNEL_CS;
  ctx->rflags = DEFAULT_RFLAGS;
  ctx->rsp = (uintptr_t) stack + PAGE_SIZE - 1;
  ctx->ss = 0;

  kprintf("new process stack at %p\n", ctx->rsp);
  kprintf("new process context at %p\n", ctx);

  process_t *process = kmalloc(sizeof(process_t));
  process_init(process);
  process->ctx = ctx;
}

void print_debug_process(process_t *process) {
  kprintf("process:\n");
  kprintf("  pid: %llu\n", process->pid);
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
