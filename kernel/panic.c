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

_used noreturn void panic_other_cpus(cpu_irq_stack_t *frame, cpu_registers_t *regs) {
  cpu_disable_interrupts();

  mtx_spin_lock(&panic_lock);
  kprintf(">>>> STOPPING CPU#%d <<<<\n", PERCPU_ID);
  kprintf("process %d [{:str}]\n", curproc->pid, &curproc->name);
  kprintf("thread %d [{:str}]\n", curthread->tid, &curthread->name);
  debug_unwind_any(frame->rip, regs->rbp);
  mtx_spin_unlock(&panic_lock);

  while (true) {
    cpu_hlt();
  }
}

/** panic - hault the system */
noreturn void panic(const char *fmt, ...) {
  int id = curcpu_id;
  cpu_disable_interrupts();
  QEMU_DEBUG_CHARP("panic\n");
  if (panic_flags[id]) {
    kprintf(">>>> nested panic <<<<\n");
    kprintf("!!! nested panic [CPU#%d] !!!\n", id);
    va_list valist;
    va_start(valist, fmt);
    kvfprintf(fmt, valist);
    va_end(valist);
    kprintf("\n");
    goto hang;
  }
  panic_flags[id] = true;

  if (system_num_cpus > 1) {
    ipi_deliver_mode(IPI_PANIC, IPI_ALL_EXCL, (uintptr_t) panic_other_cpus, /*wait_ack=*/false);
  }

  mtx_spin_lock(&panic_lock);
  kprintf("!!!!! PANIC CPU#%d <<<<\n", id);
  kprintf(">>>>> ");
  va_list valist;
  va_start(valist, fmt);
  kvfprintf(fmt, valist);
  va_end(valist);
  kprintf(" <<<<<\n");

  proc_t *proc = curproc;
  thread_t *thread = curthread;
  if (proc && thread) {
    kprintf("process %d [{:str}]\n", proc->pid, &proc->name);
    kprintf("thread %d [{:str}]\n", thread->tid, &thread->name);
  }

  stackframe_t *frame = __builtin_frame_address(0);
  debug_unwind(frame->rip, (uintptr_t) frame->rbp);
  kprintf("==== kernel heap ====\n");
  kheap_dump_stats();
  kprintf(">>>> STOPPING CPU#%d <<<<\n", id);
  mtx_spin_unlock(&panic_lock);

LABEL(hang);
  while (true) {
    cpu_hlt();
  }
}
