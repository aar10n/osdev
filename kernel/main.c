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
#include <debug/debug.h>
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
#include <fs/utils.h>

#include <device/pit.h>
#include <device/hpet.h>
#include <acpi/pm_timer.h>

#include <gui/screen.h>
#include <string.h>

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
  debug_early_init();
  screen_init();

  irq_init();
  init_mem_zones();
  init_address_space();
  debug_init();

  clock_init();
  events_init();

  syscalls_init();
  // smp_init();

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
  usb_init();
  pcie_discover();

  // memset((void *) FRAMEBUFFER_VA, 0xFF, boot_info_v2->fb_size);
  // screen_print_str("Hello, world\n");

  // if (fs_mount("/", "/dev/sdb", "ext2") < 0) {
  //   panic("%s", strerror(ERRNO));
  // }

  // fs_open("/dev/stdin", O_RDONLY, 0);
  // fs_open("/dev/stdout", O_WRONLY, 0);
  // fs_open("/dev/stderr", O_WRONLY, 0);
  // process_execve("/usr/bin/hello", (void *) argv, NULL);

  // const uint32_t ms = 1000;
  // kprintf("sleeping for %u ms\n", ms);
  // thread_sleep(MS_TO_US(ms));
  // kprintf("done!\n");

  kprintf("haulting...\n");
  thread_block();
  unreachable;
}
