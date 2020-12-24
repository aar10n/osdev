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

boot_info_t *boot_info;

_Thread_local int test;

extern void user_start();
extern void user_end();
volatile uint64_t *value = (volatile uint64_t *) 0x10000;

extern heap_t *kheap;
noreturn void fibonacci() {
  uint64_t t1, t2, next;

  t1 = 0;
  t2 = 1;
  for (int i = 1; i < 30; i++) {
    kprintf("[pid %d] %d\n", current->pid, t1);
    next = t1 + t2;
    t1 = t2;
    t2 = next;

    uint64_t count = 0;
    while (count < 1000000) {
      cpu_pause();
      count++;
    }
  }

  kprintf("[pid %d] >>> done! <<<\n", current->pid);
  print_debug_process(current);
  sched_terminate();
  while (true)
    cpu_pause();
}

noreturn void counter() {
  uint64_t count = 0;
  for (uint64_t i = 0; i <= 50000000; i++) {
    if (i % 1000000 == 0) {
      kprintf("[pid %d] count %llu\n", current->pid, i);
      count++;
      sched_yield();
    }
    cpu_pause();
  }

  kprintf("[pid %d] >>> done! <<<\n", current->pid);
  cli();
  print_debug_process(current);
  cpu_hlt();
  while (true) {
    cpu_pause();
  }
}

//
// Kernel launch process
//

noreturn void launch() {
  sti();
  kprintf("[CPU#%d] launch\n", ID);

  // page_t *code_page = alloc_page(PE_USER);
  // page_t *data_page = alloc_page(PE_USER | PE_WRITE);
  // page_t *stack_page = alloc_page(PE_USER | PE_WRITE);
  // vm_map_page_vaddr(0x10000, code_page);
  // vm_map_page_vaddr(0x11000, data_page);
  // vm_map_page_vaddr(0x12000, stack_page);
  //
  // uint64_t cr0 = read_cr0();
  // write_cr0(cr0 & ~(1 << 16)); // disable cr0.WP
  // memcpy((void *) 0x10000, user_start, user_end - user_start);
  // write_cr0(cr0);
  // sysret(0x10000, 0x12000);

  // launch child process
  pid_t parent = getpid();
  pid_t pid = process_fork(false);
  sched_print_stats();
  if (pid == -1) {
    panic("failed to fork process");
  }

  // process_t *process = kthread_create(counter);
  // sched_enqueue(process);

  kprintf("pid: %d\n", getpid());

  // void *entry = NULL;
  // load_elf((void *) PROGRAM_VA, &entry);

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
