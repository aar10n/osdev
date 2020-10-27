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

noreturn void fibonacci() {
  uint64_t t1, t2, next;

  t1 = 0;
  t2 = 1;
  for (int i = 1; i < 30; i++) {
    kprintf("[pid %d] %d\n", PERCPU->current->pid, t1);
    next = t1 + t2;
    t1 = t2;
    t2 = next;

    uint64_t count = 0;
    while (count < 1000000) {
      cpu_pause();
      count++;
    }
  }

  kprintf("[pid %d] >>> done! <<<\n", PERCPU->current->pid);
  print_debug_process(PERCPU->current);
  sched_terminate();
  while (true)
    cpu_pause();
}

noreturn void counter() {
  uint64_t count = 0;
  for (uint64_t i = 0; i <= 50000000; i++) {
    if (i % 1000000 == 0) {
      kprintf("[pid %d] count %llu\n", PERCPU->current->pid, i);
      count++;
      sched_yield();
    }
    cpu_pause();
  }

  kprintf("[pid %d] >>> done! <<<\n", PERCPU->current->pid);
  cli();
  print_debug_process(PERCPU->current);
  cpu_hlt();
  while (true) {
    cpu_pause();
  }
}

__used void kmain(boot_info_t *info) {
  boot_info = info;
  percpu_init();
  enable_sse();

  serial_init(COM1);
  kprintf("[kernel] initializing\n");

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

  // timer_init();
  // sched_init();
  // sched_enqueue(create_process(fibonacci));
  // sched_enqueue(create_process(counter));
  kprintf("[kernel] done!\n");
}

__used void ap_main() {
  percpu_init();
  enable_sse();

  kprintf("[CPU#%d] initializing\n", PERCPU->id);

  setup_gdt();
  setup_idt();

  vm_init();
  apic_init();
  ioapic_init();

  for (int i = 0; i < 10; i++) {
    kprintf("[CPU#%d] Hello, world!\n", PERCPU->id);
    uint64_t count = 0;
    while (count < 1000000) {
      cpu_pause();
      count++;
    }
  }

  kprintf("[CPU#%d] done!\n", PERCPU->id);
}
