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
#include <cpu/cpu.h>
#include <cpu/io.h>
#include <debug/debug.h>
#include <gui/screen.h>

#include <printf.h>
#include <panic.h>

// This relates to custom qemu patch that ive written to make debugging easier.
#define QEMU_DEBUG_INIT() ({ outb(0x801, 1); })

bool is_smp_enabled = false;
bool is_debug_enabled = true;
boot_info_v2_t __boot_data *boot_info_v2;

noreturn void root();

#include <vfs/vcache.h>
#include <vfs/ventry.h>
#include <vfs/vnode.h>
#include <vfs/vfs.h>

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

int command_line_main();

id_t last_id = 0;

static inline ventry_t *make_ventry(vfs_t *vfs, const char *name, enum vtype type) __move {
  vnode_t *vn = vn_alloc_empty(type);
  vn->id = ++last_id;
  vfs_add_vnode(vfs, vn);
  ventry_t *ve = ve_alloc_linked(cstr_make(name), vn);
  ve_syncvn(ve);
  vn_release(&vn);
  return ve_moveref(&ve);
}

static inline void cache_entry(vcache_t *vcache, const char *path, __move ventry_t *ve) {
  vcache_put(vcache, cstr_make(path), ve);
  ve_release(&ve);
}

noreturn void root() {
  kprintf("starting root process\n");
  alarms_init();
  do_module_initializers();
  // probe_all_buses();

  //////////////////////////////////////////

  // int fd = fs_mkdir("/test", 0777);
  // if (fd < 0) {
  //   panic("mkdir failed: {:err}\n", fd);
  // }
  //
  // fd = fs_open("/test/test.txt", O_CREAT | O_RDWR, 0777);
  // if (fd < 0) {
  //   panic("open failed: {:err}\n", fd);
  // }
  //
  // ssize_t nbytes = fs_write(fd, "hello world\n", 12);
  // if (nbytes < 0) {
  //   panic("write failed: {:err}\n", nbytes);
  // }
  //
  // int res = fs_close(fd);
  // if (res < 0) {
  //   panic("close failed: {:err}\n", res);
  // }

  kprintf("it worked!\n");

  //////////////////////////////////////////

  kprintf("haulting...\n");
  thread_block();
  unreachable;
}
