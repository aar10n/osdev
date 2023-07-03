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
#include <kernel/loader.h>

// custom qemu patch
#define QEMU_DEBUG_INIT() ({ outb(0x801, 1); })

noreturn void root();

bool is_smp_enabled = false;
bool is_debug_enabled = false;
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
  // cpu_late_init();

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

#include <kernel/vfs/path.h>
#include <bitmap.h>

uintptr_t base = 0x1d57000;

static void print_bitmap(bitmap_t *bmp) {
  uint64_t v0 = bmp->map[0];
  for (int i = 0; i < 64; i++) {
    debug_kputs((v0 & 1) ? "!" : ".");
    v0 >>= 1;
    if (i % 8 == 7) {
      debug_kputs(" ");
    }
  }
  debug_kputs("\n");
}

static inline void test_alloc_bitmap(bitmap_t *bmp, size_t n) {
  index_t i;
  if (n == 1) {
    i = bitmap_get_set_free(bmp);
  } else {
    i = bitmap_get_set_nfree(bmp, n, 0);
  }

  if (n < 0) {
    kprintf("failed to allocate %d pages\n", n);
  } else {
    uintptr_t first = base + i * PAGE_SIZE;
    uintptr_t last = first + ((n-1) * PAGE_SIZE);
    kprintf("allocated %-2d page(s) (first=%p, last=%p) | ", n, first, last);
    print_bitmap(bmp);
  }
}

static inline void test_free_bitmap(bitmap_t *bmp, index_t i, size_t n) {
  if (n == 1) {
    bitmap_clear(bmp, i);
  } else {
    bitmap_clear_n(bmp, i, n);
  }

  uintptr_t first = base + i * PAGE_SIZE;
  uintptr_t last = first + ((n-1) * PAGE_SIZE);
  // kprintf("freed %d page(s) (first=%p, last=%p)\n", n, first, last);
  // kprintf("  %064llb\n", bmp->map[0]);
  kprintf("    freed %-2d page(s) (first=%p, last=%p) | ", n, first, last);
  print_bitmap(bmp);
}

noreturn void root() {
  kprintf("starting root process\n");
  alarms_init();
  do_module_initializers();
  // probe_all_buses();

  //////////////////////////////////////////

  bitmap_t *bmp = create_bitmap(128);
  kprintf("%-54s | ", "before");
  print_bitmap(bmp);
  test_alloc_bitmap(bmp, 25);
  test_alloc_bitmap(bmp, 4);
  test_alloc_bitmap(bmp, 3);
  test_alloc_bitmap(bmp, 1);
  test_alloc_bitmap(bmp, 1);
  test_free_bitmap(bmp, 29, 3);
  test_alloc_bitmap(bmp, 4);
  test_alloc_bitmap(bmp, 5);

  // vm_print_address_space();
  //
  // if (!is_debug_enabled) {
  //   page_t *extra_page = alloc_pages(1);
  // }
  //
  // mkdir("/dev");
  // mknod("/dev/rd0", 0777|S_IFBLK, makedev(1, 0));
  //
  // ls("/dev");
  // mkdir("/initrd");
  //
  // page_t *p1 = alloc_pages(SIZE_TO_PAGES(IST_STACK_SIZE));
  // vm_mapping_t *vm1 = vmap_pages(p1, 0, IST_STACK_SIZE, VM_WRITE, "test");
  // kprintf(">>>>> test = %p\n", vm1->address);
  // // kprintf(">>>>> test = %p\n", 0);
  //
  // mount("/dev/rd0", "/initrd", "initrd", 0);
  //
  // // stat("/initrd/usr/include/sys/uio.h");
  // // cat("/initrd/usr/include/sys/uio.h");
  //
  // process_execve("/initrd/sbin/init", NULL, NULL);

  kprintf("it worked!\n");

  //////////////////////////////////////////

  kprintf("haulting...\n");
  thread_block();
  unreachable;
}
