//
// Created by Aaron Gill-Braun on 2020-09-24.
//

#include <kernel/alarm.h>
#include <kernel/clock.h>
#include <kernel/console.h>
#include <kernel/device.h>
#include <kernel/fs.h>
#include <kernel/init.h>
#include <kernel/irq.h>
#include <kernel/mm.h>
#include <kernel/params.h>
#include <kernel/proc.h>
#include <kernel/sched.h>

#include <kernel/acpi/acpi.h>
#include <kernel/cpu/cpu.h>

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

  // parse boot command line options and initalize kernel parameters.
  init_kernel_params();

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
  // debug_init();
  cpu_late_init();

  // initialize the irq layer and our clock source so the static initializers can use them.
  // we also initialize the alarm source, but it says disabled until later.
  irq_init();
  clock_init();
  alarm_init();

  // now run the static initializers.
  do_static_initializers();

  fs_init();
  sched_init();
  // smp_init(); // not working yet

  // run the module initializers followed by the last of the filesystem setup.
  do_module_initializers();
  fs_setup_final();
  kprintf("done at %llu!\n", clock_get_nanos());
  ls("/");

  cpu_enable_interrupts();
  // probe_all_buses();
  console_init();

  alarm_source_enable(alarm_tick_source());
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

//

void kernel_process1() {
#define PRINTF(fmt, ...) kprintf("kernel_process1: " fmt, ##__VA_ARGS__)
  PRINTF("starting process {:td}\n", curthread);
  PRINTF("now = %llu\n", clock_get_nanos());
  struct spin_delay delay = new_spin_delay(SHORT_DELAY, 5);
  while (spin_delay_wait(&delay));
  PRINTF("now = %llu\n", clock_get_nanos());

  PRINTF("sleeping for 1 second\n");
  alarm_sleep_ms(1000);
  PRINTF("done!\n");

  PRINTF("sleeping for 10 ms\n");
  alarm_sleep_ms(10);
  PRINTF("done!\n");
  WHILE_TRUE;
#undef PRINTF
}
