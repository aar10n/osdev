//
// Created by Aaron Gill-Braun on 2020-09-24.
//

#include <kernel/base.h>
#include <kernel/console.h>
#include <kernel/irq.h>
#include <kernel/init.h>
#include <kernel/mm.h>
#include <kernel/fs.h>
#include <kernel/device.h>
#include <kernel/syscall.h>
#include <kernel/process.h>
#include <kernel/thread.h>
#include <kernel/smpboot.h>
#include <kernel/timer.h>
#include <kernel/sched.h>

#include <kernel/acpi/acpi.h>
#include <kernel/cpu/cpu.h>
#include <kernel/cpu/io.h>
#include <kernel/debug/debug.h>
#include <kernel/gui/screen.h>

#include <kernel/printf.h>
#include <kernel/panic.h>
#include <kernel/fs_utils.h>
#include <kernel/loader.h>

// custom qemu patch
#define QEMU_DEBUG_INIT() ({ outb(0x801, 1); })

noreturn void root();

bool is_smp_enabled = false;
bool is_debug_enabled = true;
boot_info_v2_t __boot_data *boot_info_v2;


//
// Kernel entry
//

__used void kmain() {
  QEMU_DEBUG_INIT();
  console_early_init();
  cpu_init();

  // We now have primitive debugging via the serial port. In order to initialize
  // the real kernel memory allocators we need basic physical memory allocation
  // and a kernel heap. We also need to read the acpi tables and reserve virtual
  // address space for a number of memory regions.
  mm_early_init();
  irq_early_init();
  acpi_early_init();
  screen_early_init();
  debug_early_init();

  // The next step is to set up our irq abstraction layer and the physical and
  // virtual memory managers. Then we switch to a new kernel address space.
  irq_init();
  init_mem_zones();
  init_address_space();

  syscalls_init();
  fs_early_init();

  do_static_initializers();

  // Initialize debugging info early before we enter the root process.
  debug_init();

  // All of the 'one-time' initialization is now complete. We will
  // now boot up the other CPUs (if enabled) and then finish kernel
  // initialization by switching to the root process.
  smp_init();

  // Finally initialize the filesystem and the root process.
  fs_init();
  process_create_root(root);

  // This is the last step of the early kernel initialization. We now need to
  // start the scheduler and switch to the root process at which point we can
  // begin to initialize the core subsystems and drivers.
  cpu_enable_interrupts();
  sched_init();
  unreachable;
}

__used void ap_main() {
  cpu_init();
  kprintf("[CPU#%d] initializing\n", PERCPU_ID);

  init_ap_address_space();
  syscalls_init();

  kprintf("[CPU#%d] done!\n", PERCPU_ID);

  cpu_enable_interrupts();
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

  page_t *pages = alloc_pages(SIZE_TO_PAGES(SIZE_16KB));
  vm_mapping_t *vm = vmap_pages(pages, 0, SIZE_16KB, VM_READ | VM_WRITE, "test");
  vm_print_address_space();

  kprintf("updating mapping\n");
  vm_update(vm, SIZE_4KB, SIZE_4KB, VM_READ | VM_EXEC);
  vm_print_address_space();
  vm_update(vm, 0, SIZE_4KB, 0);
  vm_print_address_space();

  // mkdir("/dev");
  // mknod("/dev/rd0", 0777|S_IFBLK, makedev(1, 0));
  //
  // ls("/dev");
  // mkdir("/initrd");
  // mount("/dev/rd0", "/initrd", "initrd", 0);
  //
  // stat("/initrd/usr/include/sys/uio.h");
  // cat("/initrd/usr/include/sys/uio.h");

  // vm_print_address_space();

  // process_execve("/initrd/sbin/init", NULL, NULL);

  kprintf("it worked!\n");

  //////////////////////////////////////////

  kprintf("haulting...\n");
  thread_block();
  unreachable;
}
