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

void launch_init_process();

bool is_smp_enabled = false;
bool is_debug_enabled = true;
boot_info_v2_t __boot_data *boot_info_v2;

//
// Kernel entry
//

_used void kmain() {
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
  fs_setup_mounts();

  cpu_enable_interrupts();
  // probe_all_buses();
  // console_init();
  kprintf("{:$=^49}\n");
  kprintf("    kernel initialization done after {:llu}ms    \n", NS_TO_MS(clock_get_nanos()));
  kprintf("{:$=^49}\n");

  // give other processes a chance to run including the devfs process
  // which wll populate devices we will need shortly.
  sched_again(SCHED_YIELDED);

  ls("/");
  ls("/dev");

//  alarm_source_enable(alarm_tick_source());
  launch_init_process();
  sched_again(SCHED_BLOCKED);
  unreachable;
}

_used void ap_main() {
  cpu_early_init();
  do_percpu_initializers();
  kprintf("[CPU#%d] initializing\n", curcpu_id);

  init_ap_address_space();

  kprintf("[CPU#%d] done!\n", curcpu_id);

  sched_init();
  unreachable;
}

//

void launch_init_process() {
#define FAIL_IF_ERROR(msg, ...) \
  if (res < 0) {                \
    kprintf("launch_init_process: " msg "\n", ##__VA_ARGS__); \
    goto fail;                  \
  }

  int res;
  kprintf("launching init process\n");
  proc_t *init_proc = proc_alloc_new(getref(curproc->creds));
  proc_setup_add_thread(init_proc, thread_alloc(0, SIZE_16KB));

  res = proc_setup_exec_args(init_proc, (const char *[]){"/sbin/init", "hello", "world", NULL});
  FAIL_IF_ERROR("proc_setup_exec_args failed: {:err}", res);
  res = proc_setup_exec_env(init_proc, (const char *[]) {"TTY=/dev/ttyS2", "SHELL=/sbin/shell", NULL});
  FAIL_IF_ERROR("proc_setup_exec_env failed: {:err}", res);
  res = proc_setup_exec(init_proc, cstr_make("/sbin/init"));
  FAIL_IF_ERROR("proc_setup_exec failed: {:err}", res);
  res = proc_setup_open_fd(init_proc, 0, cstr_make("/dev/null"), O_RDONLY);
  FAIL_IF_ERROR("proc_setup_open_fd 0 failed: {:err}", res);
  res = proc_setup_open_fd(init_proc, 1, cstr_make("/dev/debug"), O_RDWR|O_NOCTTY);
  FAIL_IF_ERROR("proc_setup_open_fd 1 failed: {:err}", res);
  res = proc_setup_open_fd(init_proc, 2, cstr_make("/dev/debug"), O_RDWR|O_NOCTTY);
  FAIL_IF_ERROR("proc_setup_open_fd 2 failed: {:err}", res);
  proc_finish_setup_and_submit_all(init_proc);
  return;
LABEL(fail);
  kprintf("failed to launch init process\n");
  pr_putref(&init_proc);
#undef FAIL_IF_ERROR
}
