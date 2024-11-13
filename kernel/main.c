//
// Created by Aaron Gill-Braun on 2020-09-24.
//

#include <kernel/base.h>
#include <kernel/irq.h>
#include <kernel/init.h>
#include <kernel/clock.h>
#include <kernel/mm.h>
#include <kernel/fs.h>
#include <kernel/exec.h>
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
  cpu_late_init();

  // initialize the irq layer and timekeeping so the static initializers can use them.
  irq_init();
  clock_init();
  // then run the static initializers.
  do_static_initializers();

  fs_init();
  sched_init();

  // smp_init();

  proc_alloc_pid(); // reserve pid 1
  do_module_initializers();
  kprintf("done!\n");
  // ---------------

  mkdir("/initrd");
  mknod("/rd0", S_IFBLK, makedev(1, 0));

  mount("/rd0", "/initrd", "initrd", 0);
  replace_root("/initrd");
  unmount("/");

  mkdir("/dev");
  mknod("/dev/stdin", S_IFCHR, makedev(3, 0)); // null
  mknod("/dev/stdout", S_IFCHR, makedev(2, 2)); // com3
  mknod("/dev/stderr", S_IFCHR, makedev(2, 3)); // com4
  ls("/");

  proc_t *proc = proc_alloc_empty(1, vm_new_uspace(), getref(curproc->creds));
  proc_setup_add_thread(proc, thread_alloc(0, SIZE_16KB));
  proc_setup_new_env(proc, (const char *[]){"PWD=/", "PATH=/bin:/sbin", NULL});
  proc_setup_exec_args(proc, (const char *[]){"hello", "world", NULL});
  proc_setup_exec(proc, cstr_make("/sbin/init"));
  proc_setup_open_fd(proc, 0, cstr_make("/dev/stdin"), O_RDONLY);
  proc_setup_open_fd(proc, 1, cstr_make("/dev/stdout"), O_WRONLY);
  proc_setup_open_fd(proc, 2, cstr_make("/dev/stderr"), O_WRONLY);
  proc_finish_setup_and_submit_all(proc);

  sched_again(SCHED_BLOCKED);
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
