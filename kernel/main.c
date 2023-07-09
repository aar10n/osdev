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
  cpu_early_init();

  // We now have primitive debugging via the serial port. In order to initialize
  // the real kernel memory allocators we need basic physical memory allocation
  // and a kernel heap. We also need to read the acpi tables and reserve virtual
  // address space for a number of memory regions.
  mm_early_init();
  irq_early_init();
  acpi_early_init();
  screen_early_init();
  debug_early_init();
  fs_early_init();

  // The next step is to set up our irq abstraction layer and the physical and
  // virtual memory managers. Then we switch to a new kernel address space.
  irq_init();
  init_mem_zones();
  init_address_space();
  cpu_late_init();

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
  cpu_early_init();
  kprintf("[CPU#%d] initializing\n", PERCPU_ID);

  init_ap_address_space();
  cpu_late_init();

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

  vm_mapping_t *vm = vmap_anon(SIZE_1GB, 0, 0, VM_WRITE|VM_USER, "data");
  vm_print_address_space();
  if (vm_resize(vm, SIZE_16KB, false) != 0) {
    panic("failed to resize mapping");
  }
  vm_print_address_space();

  char *ptr = (void *) vm->address;
  kprintf("write 1\n");
  ptr[PAGES_TO_SIZE(0)] = 'a';
  kprintf("write 2\n");
  ptr[PAGES_TO_SIZE(1)] = 'b';
  kprintf("write 3\n");
  ptr[PAGES_TO_SIZE(3)] = 'c';

  if (vm_resize(vm, SIZE_1MB, false) != 0) {
    panic("failed to resize mapping");
  }

  kprintf("write 4\n");
  ptr[PAGES_TO_SIZE(17)] = 'd';

  // mkdir("/dev");
  // mknod("/dev/rd0", 0777|S_IFBLK, makedev(1, 0));
  //
  // mkdir("/initrd");
  // mount("/dev/rd0", "/initrd", "initrd", 0);
  // ls("/initrd");
  //
  // char *const args[] = {"/initrd/sbin/init", "hello", "world"};
  // process_execve("/initrd/sbin/init", args, NULL);

  //////////////////////////////////////////

  kprintf("it worked!\n");
  kprintf("haulting...\n");
  thread_block();
  unreachable;
}
