//
// Created by Aaron Gill-Braun on 2020-09-24.
//

#include <base.h>
#include <printf.h>
#include <panic.h>

#include <cpu/cpu.h>

#include <fs.h>
#include <mm.h>
#include <mm/init.h>
#include <acpi/acpi.h>
#include <console.h>
#include <syscall.h>
#include <clock.h>
#include <timer.h>
#include <sched.h>
#include <bus/pcie.h>
#include <usb/usb.h>
#include <irq.h>
#include <event.h>
#include <signal.h>
#include <ipc.h>
#include <smpboot.h>

#include <device/pit.h>
#include <device/hpet.h>

boot_info_v2_t __boot_data *boot_info_v2;

const char *argv[] = {
  "/usr/bin/hello",
  NULL
};

_Noreturn void launch();

//
// Kernel entry
//

__used void kmain() {
  console_early_init();
  cpu_init();

  mm_early_init();
  irq_early_init();
  acpi_early_init();

  irq_init();
  init_mem_zones();
  init_address_space();

  clock_init();
  events_init();

  syscalls_init();
  // smp_init();

  // init_periodic_timer();
  // timer_setval(TIMER_PERIODIC, MS_TO_NS(3000));
  // timer_enable(TIMER_PERIODIC);
  // cpu_enable_interrupts();

// #define CONV_MS(ms) (MS_TO_NS(ms) / 279)
//   clock_t target = acpi_read_pm_timer() + CONV_MS(1000);
//   while (acpi_read_pm_timer() < target) {
//     cpu_pause();
//   }
//
//   hpet_print_debug_registers();
//   kprintf("haulting...\n");
//   while (true) cpu_pause();

  cpu_enable_interrupts();
  process_t *root = process_create_root(launch);
  sched_init(root);
  unreachable;
}

__used void ap_main() {
  cpu_init();
  kprintf("[CPU#%d] initializing\n", PERCPU_ID);

  init_ap_address_space();
  syscalls_init();

  kprintf("[CPU#%d] done!\n", PERCPU_ID);

  cpu_enable_interrupts();
  sched_init(NULL);
  unreachable;
}

//
// Launch process
//

_Noreturn void launch() {
  kprintf("[pid %d] launch\n", getpid());
  alarms_init();

  fs_init();
  pcie_discover();

  // usb_init();

  // if (fs_mount("/", "/dev/sdb", "ext2") < 0) {
  //   panic("%s", strerror(ERRNO));
  // }

  // fs_open("/dev/stdin", O_RDONLY, 0);
  // fs_open("/dev/stdout", O_WRONLY, 0);
  // fs_open("/dev/stderr", O_WRONLY, 0);
  // process_execve("/usr/bin/hello", (void *) argv, NULL);

  // clock_t time0 = cpu_read_tsc();
  // clock_t time1 = cpu_read_tsc();
  // clock_t time2 = cpu_read_tsc();
  // cpu_pause();
  // cpu_pause();
  // cpu_pause();
  // cpu_pause();
  // clock_t time3 = cpu_read_tsc();
  // clock_t time4 = cpu_read_tsc();
  // kprintf("time0: %llu\n", time0);
  // kprintf("time1: %llu\n", time1);
  // kprintf("time2: %llu\n", time2);
  // kprintf("time3: %llu\n", time3);
  // kprintf("time4: %llu\n", time4);

  const uint32_t ms = 1000;
  kprintf("sleeping for %u ms\n", ms);
  thread_sleep(MS_TO_US(ms));
  kprintf("done!\n");

  // timer_udelay(1e6);
  // kprintf("time now: %llu\n", clock_now());

  // kprintf("sleeping...\n");
  // thread_sleep(100000);
  // kprintf("done\n");
  // proc_print_thread_stats(process_get(0));

  kprintf("haulting...\n");
  while (true) cpu_pause();
  // thread_block();
  unreachable;
}
