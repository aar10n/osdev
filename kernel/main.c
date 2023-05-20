//
// Created by Aaron Gill-Braun on 2020-09-24.
//

#include <base.h>
#include <console.h>
#include <irq.h>
#include <init.h>
#include <mm.h>
#include <fs.h>
#include <device.h>
#include <syscall.h>
#include <process.h>
#include <thread.h>
#include <smpboot.h>
#include <timer.h>
#include <sched.h>

#include <acpi/acpi.h>
#include <bus/pcie.h>
#include <cpu/cpu.h>
#include <cpu/io.h>
#include <debug/debug.h>
#include <usb/usb.h>
#include <gui/screen.h>

#include <printf.h>
#include <panic.h>

#include <path.h>
#include <dcache.h>
#include <dentry.h>

// This relates to custom qemu patch that ive written to make debugging easier.
#define QEMU_DEBUG_INIT() ({ outb(0x801, 1); })

bool is_smp_enabled = false;
bool is_debug_enabled = true;
boot_info_v2_t __boot_data *boot_info_v2;
struct dentry_ops dentry_ops;

noreturn void root();

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

  do_static_initializers();

  // Initialize debugging info early before we enter the root process.
  debug_init();

  // // All of the 'one-time' initialization is now complete. We will
  // // now boot up the other CPUs (if enabled) and then finish kernel
  // // initialization by switching to the root process.
  // smp_init();

  // This is the last step of the early kernel initialization. We now need to
  // start the scheduler and switch to the root process at which point we can
  // begin to initialize the core subsystems and drivers.
  process_create_root(root);
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

#include <super.h>

int command_line_main();

noreturn void root() {
  kprintf("starting root process\n");
  alarms_init();
  do_module_initializers();
  // probe_all_buses();

  //////////////////////////////////////////

  const fs_type_t *initrd_fs = fs_get_type("initrd");
  kassert(initrd_fs != NULL);
  device_t *initrd_dev = device_get(make_rdev(1, 0, 0));
  kassert(initrd_dev != NULL);

  super_block_t *sb = sb_alloc(initrd_fs);
  dentry_t *root = d_alloc("/", 1, S_IFDIR | 0755, &dentry_ops);

  int res;
  if ((res = sb_mount(sb, root, initrd_dev)) < 0) {
    kprintf("failed to mount initrd: %s\n", strerror(res));
  }

  kprintf("it worked!\n");

  //////////////////////////////////////////

  kprintf("haulting...\n");
  thread_block();
  unreachable;
}
