//
// Created by Aaron Gill-Braun on 2020-09-24.
//

#include <base.h>
#include <console.h>
#include <irq.h>
#include <init.h>
#include <mm.h>
#include <fs.h>
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

// This relates to custom qemu patch that ive written to make debugging easier.
#define QEMU_DEBUG_INIT() ({ outb(0x801, 1); })

bool is_smp_enabled = false;
bool is_debug_enabled = true;
boot_info_v2_t __boot_data *boot_info_v2;
_Noreturn void launch();

//
// Kernel entry
//

__used void kmain() {
  QEMU_DEBUG_INIT();
  console_early_init();
  cpu_init();

  // We now have very primitive debugging via the serial port.
  // In order to initialize the real kernel memory allocators
  // we need basic physical mem allocation and a kernel heap.
  // This is also the point where we switch from the virtual
  // mappings provided by the bootloader.
  mm_early_init();
  irq_early_init();
  acpi_early_init();
  screen_early_init();
  debug_early_init();

  // The next step is to set up our physical and virtual memory
  // managers and then switch to a new managed kernel address
  // space. We also initialize a few other misc bits.
  irq_init();
  init_mem_zones();
  init_address_space();
  syscalls_init();
  screen_init();

  // Initialize debugging info sooner than later so that we
  // get helpful stacktraces in the event of a panic or fault.
  debug_init();

  // Next we want to initialize the filesystem so that drivers
  // and other subsystems can register devices, create special
  // files and expose APIs through the filesystem.
  //
  // But before we can call `fs_init` we will allocate the root
  // process and set it as the "current" process. This is done
  // so that our fs code can safely access `PERCPU_PROCESS->pwd`
  // even though were not yet running the root process.
  process_t *root = process_create_root(launch);
  PERCPU_SET_PROCESS(root);
  PERCPU_SET_THREAD(root->main);
  fs_init();

  // The kernel has now initialized the foundational set of APIs
  // needed for most normal function. At this point we can call
  // the initializer functions registered with the `MODULE_INIT`
  // macro.
  do_module_initializers();

  // All of the 'one-time' initialization is now complete. We will
  // now boot up the other CPUs (if enabled) and then finish kernel
  // initialization by switching to the root (launch) process.
  smp_init();

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

int command_line_main();

_Noreturn void launch() {
  kprintf("launch\n");
  alarms_init();

  usb_init();
  pcie_discover();

  // thread_sleep(250);

  command_line_main();
  // int ch;
  // while ((ch = kgetc()) > 0) {
  //   kputc(ch);
  // }

  kprintf("haulting...\n");
  thread_block();
  unreachable;
}
