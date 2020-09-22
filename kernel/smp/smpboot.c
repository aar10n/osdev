//
// Created by Aaron Gill-Braun on 2020-09-21.
//

#include <stdio.h>

#include <drivers/serial.h>
#include <kernel/smp/smpboot.h>

void ap_main() {
  init_serial(COM1);
  kprintf("Hello from another core!\n");
}
