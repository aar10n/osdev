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


dentry_t *create_vfs_tree() {
  dentry_t *root = d_alloc_dir("/", &dentry_ops);
  root->parent = root;
  {
    dentry_t *bin = d_alloc_dir("bin", &dentry_ops);
    d_add_child(root, bin);
    {
      d_add_child(bin, d_alloc("ls", S_IFREG, &dentry_ops));
      d_add_child(bin, d_alloc("cat", S_IFREG, &dentry_ops));
      d_add_child(bin, d_alloc("echo", S_IFREG, &dentry_ops));
    }
  }
  {
    dentry_t *home = d_alloc_dir("home", &dentry_ops);
    d_add_child(root, home);
    {
      dentry_t *aaron = d_alloc_dir("aaron", &dentry_ops);
      d_add_child(home, aaron);
      {
        d_add_child(aaron, d_alloc("hello.txt", S_IFREG, &dentry_ops));
      }
    }
  }

  return root;
}

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

int command_line_main();

noreturn void root() {
  kprintf("starting root process\n");
  alarms_init();
  // do_module_initializers();
  // probe_all_buses();

  //////////////////////////////////////////

  dentry_t *root = create_vfs_tree();
  path_t path = str2path("/home/aaron/");

  int res;
  dentry_t *aaron = NULL;
  if ((res = resolve_path(root, root, path, 0, &aaron)) < 0) {
    kprintf("failed to resolve path: {:path}\n", &path);
    kprintf("error: %d\n", res);
  } else {
    kprintf("resolve dentry: {:.*s}\n", aaron->name, aaron->namelen);
  }

  char tmp[PATH_MAX+1] = {0};
  sbuf_t buf = sbuf_init(tmp, PATH_MAX+1);
  if ((res = get_dentry_path(root, aaron, &buf, NULL)) < 0) {
    kprintf("failed to get dentry path [%d]\n", -res);
    WHILE_TRUE;
  }


  sbuf_reset(&buf);
  path = str2path("..");
  if ((res = expand_path(root, aaron, path, &buf)) < 0) {
    kprintf("failed to expand path: {:path} [%d]\n", &path, -res);
  } else {
    kprintf("expanded {:path} -> {:s}\n", &path, tmp);
  }

  //////////////////////////////////////////

  kprintf("haulting...\n");
  thread_block();
  unreachable;
}
