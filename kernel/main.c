//
// Created by Aaron Gill-Braun on 2020-09-24.
//

#include <kernel/base.h>
#include <kernel/irq.h>
#include <kernel/init.h>
#include <kernel/alarm.h>
#include <kernel/clock.h>
#include <kernel/mm.h>
#include <kernel/fs.h>
#include <kernel/exec.h>
#include <kernel/device.h>
#include <kernel/proc.h>
#include <kernel/smpboot.h>
#include <kernel/sched.h>

#include <kernel/acpi/acpi.h>
#include <kernel/cpu/cpu.h>
#include <kernel/debug/debug.h>
#include <kernel/usb/usb.h>

#include <kernel/printf.h>
#include <kernel/panic.h>
#include <kernel/fs_utils.h>

#include <kernel/mm/pgtable.h>

bool is_smp_enabled = false;
bool is_debug_enabled = true;
boot_info_v2_t __boot_data *boot_info_v2;

void kernel_process1();
void kernel_process2();

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
  // smp_init();

  // run the module initializers followed by the last of the filesystem setup.
  do_module_initializers();
  fs_setup_final();
  kprintf("[%llu] done!\n", clock_get_nanos());
  ls("/");

  cpu_enable_interrupts();
  probe_all_buses();

  // {
  //   __ref proc_t *proc = proc_alloc_new(getref(curproc->creds));
  //   proc_setup_add_thread(proc, thread_alloc(0, SIZE_16KB));
  //   proc_setup_new_env(proc, (const char *[]){"PWD=/", "PATH=/bin:/sbin", NULL});
  //   proc_setup_exec_args(proc, (const char *[]){"hello", "world", NULL});
  //   proc_setup_exec(proc, cstr_make("/sbin/init"));
  //   proc_setup_open_fd(proc, 0, cstr_make("/dev/stdin"), O_RDONLY);
  //   proc_setup_open_fd(proc, 1, cstr_make("/dev/stdout"), O_WRONLY);
  //   proc_setup_open_fd(proc, 2, cstr_make("/dev/stderr"), O_WRONLY);
  //   proc_finish_setup_and_submit_all(moveref(proc));
  // }

  // {
  //   __ref proc_t *kproc1 = proc_alloc_new(getref(curproc->creds));
  //   proc_setup_add_thread(kproc1, thread_alloc(TDF_KTHREAD, SIZE_16KB));
  //   proc_setup_entry(kproc1, (uintptr_t) kernel_process1, 0);
  //   proc_setup_name(kproc1, cstr_make("kernel_process1"));
  //   proc_finish_setup_and_submit_all(moveref(kproc1));
  // }

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
//
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
