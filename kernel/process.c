//
// Created by Aaron Gill-Braun on 2020-10-19.
//

#include <process.h>
#include <percpu.h>
#include <stdatomic.h>
#include <string.h>

#include <mm/heap.h>
#include <mm/mm.h>
#include <mm/vm.h>
#include <stdio.h>

static _Atomic uint64_t __pid = 0;

uint64_t alloc_pid() {
  return atomic_fetch_add(&__pid, 1);
}

void process_init(process_t *process) {
  process->pid = alloc_pid();
  process->ctx = NULL;
  process->cpu_id = PERCPU->id;
  process->priority = 0;
  process->runtime = 0;
  process->state = READY;
  process->next = NULL;
}


void kthread_create(void (*func)()) {
  kprintf("[process] creating thread\n");
  // allocate a process stack
  page_t *page = mm_alloc_page(ZONE_NORMAL, PE_WRITE);
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
  schedl_schedule(process);
}
