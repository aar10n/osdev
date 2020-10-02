//
// Created by Aaron Gill-Braun on 2020-09-05.
//

#include <base.h>
#include <stdarg.h>
#include <stdio.h>
#include <cpu/cpu.h>
#include <panic.h>

/** panic - hault the system */
noreturn void panic(const char *fmt, ...) {
  disable_interrupts();

  va_list valist;
  va_start(valist, fmt);
  kvfprintf(fmt, valist);
  va_end(valist);
  kprintf("\n");

  while (1) {
    asm("hlt");
  }
}
