//
// Created by Aaron Gill-Braun on 2020-09-24.
//

#include <kernel/base.h>
#include <kernel/irq.h>
#include <kernel/init.h>
#include <kernel/clock.h>
#include <kernel/mm.h>
#include <kernel/fs.h>
#include <kernel/device.h>
#include <kernel/proc.h>
#include <kernel/smpboot.h>
#include <kernel/sched.h>

#include <kernel/acpi/acpi.h>
#include <kernel/cpu/cpu.h>
#include <kernel/debug/debug.h>

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
  // before anything else we need to make sure we can use panic, kprintf.
  panic_early_init();
  kprintf_early_init();
  // initialize the cpu and populate the percpu cpu_info struct, followed by the kernel
  // heap, and early memory apis.
  cpu_early_init();
  mm_early_init();
  do_percpu_initializers();

  // initialize acpi information while the bootloader provided identity mappings are still
  // active so we can reserve the memory we need to keep mapped while allowing the rest to
  // be reclaimed.
  acpi_early_init();

  // now we can run the registered initializers and end the early initialization phase.
  do_early_initializers();

  // setup and use the proc0 context so that `curthread` and `curproc` are valid.
  proc0_init();

  // initialize physical page and the virtual memory allocation, the virtual address space
  // and finally unmap the original identity mappings.
  init_mem_zones();
  init_address_space();
  debug_init();

  // initialize the irq layer and timekeeping so the static initializers can use them.
  irq_init();
  clock_init();
  // then run the static initializers.
  do_static_initializers();

  fs_init();
  sched_init();

  // smp_init();
  kprintf("done!\n");
  unreachable;
}

__used void ap_main() {
  cpu_early_init();
  do_percpu_initializers();
  kprintf("[CPU#%d] initializing\n", curcpu_id);

  init_ap_address_space();

  kprintf("[CPU#%d] done!\n", curcpu_id);

  sched_init();
  unreachable;
}




//
// Launch process
//

// noreturn void root() {
//   kprintf("starting root process\n");
//   do_module_initializers();
//   // probe_all_buses();
//
//   //////////////////////////////////////////
//
//   mkdir("/dev");
//   mknod("/dev/rd0", S_IFBLK, makedev(1, 0));
//   mknod("/dev/tty0", S_IFCHR, makedev(2, 0));
//   mknod("/dev/tty1", S_IFCHR, makedev(2, 1));
//   mknod("/dev/tty2", S_IFCHR, makedev(2, 2));
//   mknod("/dev/tty3", S_IFCHR, makedev(2, 3));
//   mknod("/dev/null", S_IFCHR, makedev(3, 0));
//   mknod("/dev/debug", S_IFCHR, makedev(3, 1));
//
//   mkdir("/initrd");
//   mount("/dev/rd0", "/initrd", "initrd", 0);
//   ls("/initrd");
//
//   open("/dev/null", O_RDONLY|O_SYNC); // fd0=stdin
//   open("/dev/tty2", O_WRONLY|O_SYNC); // fd1=stdout
//   open("/dev/tty2", O_WRONLY|O_SYNC); // fd2=stderr
//   // char *const args[] = {"/initrd/sbin/init", "hello", "world"};
//   // process_execve("/initrd/sbin/init", args, NULL);
//
//   //////////////////////////////////////////
//
//   kprintf("it worked!\n");
//   kprintf("haulting...\n");
//   WHILE_TRUE;
// }
