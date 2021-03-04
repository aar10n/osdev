//
// Created by Aaron Gill-Braun on 2020-09-24.
//

#include <base.h>
#include <stdio.h>
#include <panic.h>

#include <cpu/cpu.h>
#include <cpu/gdt.h>
#include <cpu/idt.h>

#include <mm/mm.h>
#include <mm/vm.h>
#include <mm/heap.h>

#include <acpi.h>
#include <percpu.h>
#include <smpboot.h>
#include <syscall.h>
#include <timer.h>
#include <scheduler.h>

#include <drivers/serial.h>
#include <drivers/ahci.h>

#include <device/apic.h>
#include <device/ioapic.h>
#include <device/pic.h>

#include <loader.h>
#include <fs.h>
#include <fs/proc/proc.h>
#include <fs/utils.h>

#include <usb/xhci.h>


boot_info_t *boot_info;

//
// Kernel launch process
//

noreturn void launch() {
  // sti();
  cli();
  kprintf("[pid %d] launch\n", ID);
  fs_init();

  pci_enumerate_busses();

  // xhci_init();

  kprintf("[pid %d] done!\n", current->pid);
  while (true) {
    cpu_pause();
  }
}


//
// Kernel entry
//

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

  syscalls_init();
  // smp_init();

  // root process
  process_t *root = kthread_create(launch);

  timer_init();
  sched_init();
  sched_enqueue(root);
  sched_print_stats();
  sched_schedule();
}

__used void ap_main() {
  percpu_init();
  enable_sse();

  kprintf("[CPU#%d] initializing\n", ID);

  setup_gdt();
  setup_idt();

  vm_init();
  apic_init();
  ioapic_init();

  kprintf("[CPU#%d] done!\n", ID);
  // for (int i = 0; i < 10; i++) {
  //   uint64_t count = 0;
  //   while (count < 1000000) {
  //     cpu_pause();
  //     count++;
  //   }
  // }
  //
  // kprintf("[CPU#%d] done!\n", ID);
}
