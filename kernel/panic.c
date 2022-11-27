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

  uintptr_t rip = (uintptr_t) __builtin_return_address(0);
  uintptr_t rbp = (uintptr_t) __builtin_frame_address(0);
  debug_unwind(rip, rbp);

  ipi_deliver_mode(IPI_PANIC, IPI_ALL_EXCL, 0);
  while (true) {
    cpu_hlt();
  }
}
