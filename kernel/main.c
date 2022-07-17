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
#include <scheduler.h>
#include <bus/pcie.h>
#include <usb/usb.h>
#include <irq.h>
#include <event.h>
#include <signal.h>
#include <ipc.h>
#include <smpboot.h>

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
  cpu_map_topology();

  irq_init();
  init_mem_zones();
  init_address_space();

  clock_init();
  events_init();

  syscalls_init();
  smp_init();

  // root process
  process_t *root = process_create_root(launch);
  scheduler_init(root);
  unreachable;
}

__used void ap_main() {
  kprintf("[CPU#%d] initializing\n", PERCPU_ID);
  cpu_init();
  kprintf("[CPU#%d] done!\n", PERCPU_ID);
  while (true) {
    cpu_hlt();
  }
}

//
// Launch process
//

_Noreturn void launch() {
  kprintf("[pid %d] launch\n", getpid());
  cpu_enable_interrupts();
  alarms_init();

  fs_init();
  pcie_discover();

  usb_init();

  if (fs_mount("/", "/dev/sdb", "ext2") < 0) {
    panic("%s", strerror(ERRNO));
  }

  fs_open("/dev/stdin", O_RDONLY, 0);
  fs_open("/dev/stdout", O_WRONLY, 0);
  fs_open("/dev/stderr", O_WRONLY, 0);
  process_execve("/usr/bin/hello", (void *) argv, NULL);

  kprintf("haulting...\n");
  thread_block();
  unreachable;
}
