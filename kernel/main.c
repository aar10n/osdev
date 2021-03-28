//
// Created by Aaron Gill-Braun on 2020-09-24.
//

#include <base.h>
#include <printf.h>
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

#include <bus/pcie.h>
#include <usb/xhci.h>

boot_info_t *boot_info;

//
// Kernel launch process
//

void *thread1(void *arg) {
  kprintf("[pid %d:%d] thread routine\n", getpid(), gettid());
  kprintf("inside thread 1\n");
  thread_sleep(2e6); // 2 seconds
  return (void *) 0x1234;
}

void *thread2(void *arg) {
  kprintf("[pid %d:%d] thread routine\n", getpid(), gettid());
  kprintf("inside thread 2\n");
  thread_sleep(1e6); // 1 second
  return (void *) 0x5678;
}

void launch() {
  sti();

  kprintf("[pid %d] launch\n", ID);
  fs_init();

  pcie_init();
  pcie_discover();

  thread_t *t1 = thread_create(thread1, NULL);
  thread_t *t2 = thread_create(thread2, NULL);
  kprintf("[pid %d:%d] joining threads\n", getpid(), gettid());

  void *ret1, *ret2;
  thread_join(t1, &ret1);
  thread_join(t2, &ret2);

  kprintf("ret1: %p\n", ret1);
  kprintf("ret2: %p\n", ret2);

  kprintf("done!\n");
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
  process_t *root = create_root_process(launch);

  timer_init();
  scheduler_init(root);
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
}
