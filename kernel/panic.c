//
// Created by Aaron Gill-Braun on 2020-09-05.
//

#include <kernel/panic.h>
#include <kernel/cpu/asm.h>
#include <stdarg.h>
#include <stdio.h>

/** panic - hault the system */
_Noreturn void panic(const char *fmt, ...) {
  disable_interrupts();

  va_list valist;
  va_start(valist, fmt);
  kvfprintf(fmt, valist);
  va_end(valist);

  while (1) {
    __asm("hlt");
  }
}
