//
// Created by Aaron Gill-Braun on 2020-09-05.
//

#include <kernel/panic.h>
#include <kernel/ipi.h>
#include <kernel/thread.h>
#include <kernel/process.h>
#include <kernel/timer.h>
#include <kernel/mm.h>

#include <kernel/cpu/cpu.h>
#include <kernel/debug/debug.h>

#include <stdarg.h>
#include <kernel/printf.h>
#include <atomic.h>

static int panic_flag = 0;
static spinlock_t panic_lock = {};
static volatile uint32_t lock = 0;
static int panic_flags[2] = {};

noreturn void panic_other_cpus(cpu_irq_stack_t *frame, cpu_registers_t *regs) {
  cpu_disable_interrupts();
  while (atomic_lock_test_and_set(&lock) != 0) {
    cpu_pause();
  }

  kprintf(">>>> STOPPING CPU#%d <<<<\n", PERCPU_ID);
  kprintf("thread %d.%d [%s]\n", getpid(), gettid(), PERCPU_THREAD->name);
  debug_unwind(frame->rip, regs->rbp);
  atomic_lock_test_and_reset(&lock);

  while (true) {
    cpu_hlt();
  }
}

/** panic - hault the system */
noreturn void panic(const char *fmt, ...) {
  if (__builtin_expect(panic_flags[PERCPU_ID], 0) != 0) {
    kprintf("!!! nested panic [CPU#%d] !!!\n", PERCPU_ID);
    va_list valist;
    va_start(valist, fmt);
    kvfprintf(fmt, valist);
    va_end(valist);
    kprintf("\n");
    WHILE_TRUE;
  }
  panic_flags[PERCPU_ID]++;

  cpu_disable_interrupts();
  kprintf(">>>> PANIC CPU#%d [irq level = %d] <<<<\n", PERCPU_ID, __percpu_get_irq_level());
  va_list valist;
  va_start(valist, fmt);
  kvfprintf(fmt, valist);
  va_end(valist);
  kprintf("\n");
  while (atomic_lock_test_and_set(&lock) != 0) {
    cpu_pause();
  }

  thread_t *thread = PERCPU_THREAD;
  if (thread) {
    kprintf("thread %d.%d [%s]\n", thread->process->pid, thread->tid, thread->name);
  }
  stackframe_t *frame = (void *) __builtin_frame_address(0);
  debug_unwind(frame->rip, (uintptr_t) frame->rbp);
  kprintf("==== kernel heap ====\n");
  kheap_dump_stats();
  kprintf("==== timer alarms ====\n");
  timer_dump_pending_alarms();
  atomic_lock_test_and_reset(&lock);

  ipi_deliver_mode(IPI_PANIC, IPI_ALL_EXCL, (uint64_t) panic_other_cpus);

  kprintf(">>>> STOPPING CPU#%d <<<<\n", PERCPU_ID);
  while (true) {
    cpu_hlt();
  }
}
