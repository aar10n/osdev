//
// Created by Aaron Gill-Braun on 2020-09-05.
//

#include <kernel/panic.h>
#include <kernel/proc.h>
#include <kernel/printf.h>
#include <kernel/ipi.h>
#include <kernel/mm.h>

#include <kernel/cpu/cpu.h>
#include <kernel/debug/debug.h>

#include <stdarg.h>

static mtx_t panic_lock;
static bool panic_flags[MAX_CPUS] = {};

// this must be run before anything that may assert or panic, which includes
// kprintf so this is essentially the first code ran on kernel entry.
void panic_early_init() {
  mtx_init(&panic_lock, MTX_SPIN|MTX_RECURSIVE, "panic_lock");
}

noreturn void panic_other_cpus(cpu_irq_stack_t *frame, cpu_registers_t *regs) {
  cpu_disable_interrupts();
  mtx_spin_lock(&panic_lock);

  kprintf(">>>> STOPPING CPU#%d <<<<\n", PERCPU_ID);
  kprintf("thread %d:%d [{:str}]\n", curproc->pid, curthread->tid, &curthread->name);
  debug_unwind(frame->rip, regs->rbp);
  mtx_spin_unlock(&panic_lock);

  while (true) {
    cpu_hlt();
  }
}

/** panic - hault the system */
noreturn void panic(const char *fmt, ...) {
  if (panic_flags[PERCPU_ID]) {
    kprintf(">>>> nested panic <<<<\n");
    kprintf("!!! nested panic [CPU#%d] !!!\n", PERCPU_ID);
    va_list valist;
    va_start(valist, fmt);
    kvfprintf(fmt, valist);
    va_end(valist);
    kprintf("\n");
    goto hang;
  }
  panic_flags[PERCPU_ID] = true;

  kprintf("!!!!! PANIC CPU#%d <<<<\n", PERCPU_ID);
  kprintf(">>>>> ");
  va_list valist;
  va_start(valist, fmt);
  kvfprintf(fmt, valist);
  va_end(valist);
  kprintf(" <<<<<\n");
  mtx_spin_lock(&panic_lock);

  thread_t *thread = curthread;
  if (thread) {
    kprintf("thread %d:%d [%s]\n", curproc->pid, thread->tid, &thread->name);
  }
  stackframe_t *frame = (void *) __builtin_frame_address(0);
  debug_unwind(frame->rip, (uintptr_t) frame->rbp);
  kprintf("==== kernel heap ====\n");
  kheap_dump_stats();
  mtx_spin_unlock(&panic_lock);

  ipi_deliver_mode(IPI_PANIC, IPI_ALL_EXCL, (uint64_t) panic_other_cpus);

  kprintf(">>>> STOPPING CPU#%d <<<<\n", PERCPU_ID);

  LABEL(hang);
  while (true) {
    cpu_hlt();
  }
}
