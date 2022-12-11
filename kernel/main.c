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
#include <init.h>
#include <ipi.h>
#include <event.h>
#include <signal.h>
#include <ipc.h>
#include <smpboot.h>
#include <fs/utils.h>

#include <device/apic.h>
#include <device/hpet.h>
#include <device/pit.h>
#include <acpi/pm_timer.h>

#include <gui/screen.h>
#include <string.h>

#include <cpu/io.h>

// This relates to custom qemu patch that ive written to make debugging easier.
#define QEMU_DEBUG_INIT() ({ outb(0x801, 1); })

bool is_smp_enabled = false;
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

_Noreturn void launch() {
  kprintf("==> launch\n");
  alarms_init();

  usb_init();
  pcie_discover();

  thread_sleep(MS_TO_US(250));
  // fs_lsdir("/dev");
  //
  // if (fs_mount("/", "/dev/sdb", "ext2") < 0) {
  //   panic("failed to mount");
  // }
  //
  // fs_lsdir("/");

  // // memset((void *) FRAMEBUFFER_VA, 0xFF, boot_info_v2->fb_size);
  // // screen_print_str("Hello, world\n");
  //
  // if (fs_mount("/test", "/dev/sdb", "ext2") < 0) {
  //   panic("%s", strerror(ERRNO));
  // }

  // fs_open("/dev/stdin", O_RDONLY, 0);
  // fs_open("/dev/stdout", O_WRONLY, 0);
  // fs_open("/dev/stderr", O_WRONLY, 0);

  // kprintf("echoing stdin\n");
  // char ch;
  // while (fs_read(0, &ch, 1) > 0) {
  //   kprintf("%c", ch);
  // }

  // process_execve("/usr/bin/hello", (void *) argv, NULL);

  // const uint32_t ms = 1000;
  // kprintf("sleeping for %u ms\n", ms);
  // thread_sleep(MS_TO_US(ms));
  // kprintf("done!\n");

  kprintf("haulting...\n");
  thread_block();
  unreachable;
}
