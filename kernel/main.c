//
// Created by Aaron Gill-Braun on 2020-09-24.
//

#include <base.h>
#include <stdio.h>

#include <cpu/gdt.h>
#include <cpu/idt.h>

#include <mm/mm.h>
#include <mm/vm.h>
#include <mm/heap.h>

#include <acpi.h>
#include <percpu.h>
#include <smpboot.h>
#include <timer.h>
#include <scheduler.h>

#include <drivers/serial.h>

#include <device/apic.h>
#include <device/ioapic.h>
#include <device/pic.h>

boot_info_t *boot_info;

__used void kmain(boot_info_t *info) {
  boot_info = info;

  serial_init(COM1);
  kprintf("[kernel] initializing\n");

  percpu_init();
  percpu_init_cpu();

  setup_gdt();
  setup_idt();

  kheap_init();
  mm_init();
  vm_init();

  acpi_init();

  pic_init();
  apic_init();
  ioapic_init();

  smp_init();

  timer_init();
  sched_init();

  kprintf("[kernel] done!\n");
}

__used void ap_main() {
  kprintf("Hello from another core!\n");
}
