//
// Created by Aaron Gill-Braun on 2020-09-24.
//

#include <kernel/base.h>
#include <kernel/irq.h>
#include <kernel/init.h>
#include <kernel/mm.h>
#include <kernel/fs.h>
#include <kernel/device.h>
#include <kernel/process.h>
#include <kernel/thread.h>
#include <kernel/smpboot.h>
#include <kernel/timer.h>
#include <kernel/sched.h>

#include <kernel/acpi/acpi.h>
#include <kernel/cpu/cpu.h>
#include <kernel/cpu/io.h>
#include <kernel/cpu/per_cpu.h>
#include <kernel/debug/debug.h>
#include <kernel/gui/screen.h>

#include <kernel/printf.h>
#include <kernel/panic.h>
#include <kernel/fs_utils.h>

noreturn void root();

bool is_smp_enabled = false;
bool is_debug_enabled = true;
boot_info_v2_t __boot_data *boot_info_v2;

//
// Kernel entry
//

__used void kmain() {
  // Setup kprintf
  kprintf_early_init();

  // Early initialization
  cpu_early_init();
  mm_early_init();
  acpi_early_init();
  screen_early_init();
  debug_early_init();
  fs_early_init();
  irq_init();

  // Memory initialization
  init_mem_zones();
  init_address_space();
  cpu_stage2_init();
  debug_init();

  do_static_initializers();

  fs_init();
  proc_init();
  sched_init();
  // smp_init();

  unreachable;
}

__used void ap_main() {
  cpu_early_init();
  kprintf("[CPU#%d] initializing\n", PERCPU_ID);

  init_ap_address_space();
  cpu_stage2_init();

  kprintf("[CPU#%d] done!\n", PERCPU_ID);

  sched_init();
  unreachable;
}

//
// Launch process
//

noreturn void root() {
  kprintf("starting root process\n");
  alarms_init();
  do_module_initializers();
  // probe_all_buses();

  //////////////////////////////////////////

  mkdir("/dev");
  mknod("/dev/rd0", S_IFBLK, makedev(1, 0));
  mknod("/dev/tty0", S_IFCHR, makedev(2, 0));
  mknod("/dev/tty1", S_IFCHR, makedev(2, 1));
  mknod("/dev/tty2", S_IFCHR, makedev(2, 2));
  mknod("/dev/tty3", S_IFCHR, makedev(2, 3));
  mknod("/dev/null", S_IFCHR, makedev(3, 0));
  mknod("/dev/debug", S_IFCHR, makedev(3, 1));

  mkdir("/initrd");
  mount("/dev/rd0", "/initrd", "initrd", 0);
  ls("/initrd");

  open("/dev/null", O_RDONLY|O_SYNC); // fd0=stdin
  open("/dev/tty2", O_WRONLY|O_SYNC); // fd1=stdout
  open("/dev/tty2", O_WRONLY|O_SYNC); // fd2=stderr
  // char *const args[] = {"/initrd/sbin/init", "hello", "world"};
  // process_execve("/initrd/sbin/init", args, NULL);

  //////////////////////////////////////////

  kprintf("it worked!\n");
  kprintf("haulting...\n");
  WHILE_TRUE;
}
