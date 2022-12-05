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
#define QDEBUG_VALUE(v) ({ outdw(0x800, v); })

boot_info_v2_t __boot_data *boot_info_v2;

const char *argv[] = {
  "/usr/bin/hello",
  NULL
};

_Noreturn void launch();

static void dump_bytes(uint8_t *buffer, size_t length) {
  kprintf("DUMPING BYTES [buffer = %018p, length = %zu]\n", buffer, length);
  size_t index = 0;
  while (length > 0) {
    kprintf("      ");
    for (size_t i = 0; i < 32; i++) {
      kprintf("%02x ", buffer[index]);
      index++;
      length--;
    }
    kprintf("\n");
  }
}

static noreturn void *cpu_test_thread(void *arg) {
  thread_setaffinity(1);
  thread_yield();

  uint32_t timeout = 100000;
  while (timeout > 0) {
    cpu_pause();
    timeout--;
  }

  kprintf("[CPU#%d] knock knock\n", PERCPU_ID);
  ipi_deliver_cpu_id(IPI_NOOP, 0, 0);

  thread_block();
  unreachable;
}

//
// Kernel entry
//

__used void kmain() {
  QEMU_DEBUG_INIT();
  console_early_init();
  cpu_init();

  mm_early_init();
  irq_early_init();
  acpi_early_init();
  screen_early_init();
  debug_early_init();

  irq_init();
  init_mem_zones();
  init_address_space();
  screen_init();
  debug_init();

  clock_init();
  events_init();

  syscalls_init();
  smp_init();

  cpu_enable_interrupts();
  process_t *root = process_create_root(launch);
  sched_init(root);
  unreachable;
}

__used void ap_main() {
  cpu_init();
  kprintf("[CPU#%d] initializing\n", PERCPU_ID);

  init_ap_address_space();
  syscalls_init();

  kprintf("[CPU#%d] done!\n", PERCPU_ID);

  cpu_enable_interrupts();
  sched_init(NULL);
  unreachable;
}

//
// Launch process
//

extern size_t _num_schedulers;

_Noreturn void launch() {
  kprintf("==> launch\n");
  alarms_init();

  fs_init();
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
