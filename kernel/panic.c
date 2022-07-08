//
// Created by Aaron Gill-Braun on 2020-09-05.
//

#include <base.h>
#include <stdarg.h>
#include <printf.h>
#include <cpu/cpu.h>
#include <panic.h>

/** panic - hault the system */
noreturn void panic(const char *fmt, ...) {
  cpu_disable_interrupts();

  kprintf("PANIC: ");
  va_list valist;
  va_start(valist, fmt);
  kvfprintf(fmt, valist);
  va_end(valist);
  kprintf("\n");

  while (true) {
    asm("hlt");
  }
}
