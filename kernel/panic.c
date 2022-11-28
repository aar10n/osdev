//
// Created by Aaron Gill-Braun on 2020-09-05.
//

#include <panic.h>

#include <cpu/cpu.h>
#include <debug/debug.h>

#include <ipi.h>
#include <stdarg.h>
#include <printf.h>

/** panic - hault the system */
noreturn void panic(const char *fmt, ...) {
  cpu_disable_interrupts();

  kprintf("[CPU#%d] PANIC: ", PERCPU_ID);
  va_list valist;
  va_start(valist, fmt);
  kvfprintf(fmt, valist);
  va_end(valist);
  kprintf("\n");

  ipi_deliver_mode(IPI_PANIC, IPI_ALL_EXCL, 0);
  stackframe_t *frame = (void *) __builtin_frame_address(0);
  debug_unwind(frame->rip, (uintptr_t) frame->rbp);

  while (true) {
    cpu_hlt();
  }
}
