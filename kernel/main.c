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
#include <kernel/smpboot.h>

#include <kernel/acpi/acpi.h>
#include <kernel/cpu/cpu.h>
#include <kernel/debug/debug.h>

#include <kernel/printf.h>
#include <kernel/panic.h>
#include <kernel/fs_utils.h>

void launch_init_process();

boot_info_v2_t __boot_data *boot_info_v2;

KERNEL_PARAM("smp", bool, is_smp_enabled, false); // not working yet
KERNEL_PARAM("debug", bool, is_debug_enabled, false);
KERNEL_PARAM("init", str_t, init_program, str_null);
KERNEL_PARAM("init.shell", str_t, init_shell_program, str_null);
KERNEL_PARAM("init.tty", str_t, init_tty_device, str_null);

//
// Kernel entry
//

_used void kmain() {
  QEMU_DEBUG_CHARP("kmain\n");
  // before anything else we need to make sure we can use panic, kprintf.
  panic_early_init();
  kprintf_early_init();

  // initialize the cpu and populate the percpu cpu_info struct, followed by the kernel
  // heap, and early memory apis.
  cpu_early_init();
  mm_early_init();
  do_percpu_early_initializers();

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

  // perform late stage cpu related initialization that needs to allocate pages
  cpu_late_init();
  debug_init();

  // initialize the irq layer and our clock source so the static initializers can use them.
  // we also initialize the alarm source, but it says disabled until later.
  irq_init();
  clock_init();
  alarm_init();

  // now run the static initializers.
  do_static_initializers();
  do_percpu_static_initializers();

  fs_init();
  sched_init();
  smp_init();

  // run the module initializers followed by the last of the filesystem setup.
  do_module_initializers();
  fs_setup_mounts();

  cpu_enable_interrupts();
  probe_all_buses();
  alarm_source_enable(alarm_tick_source());

  kprintf("{:$=^49}\n");
  kprintf("    kernel initialization done after {:llu}ms    \n", NS_TO_MS(clock_get_nanos()));
  kprintf("{:$=^49}\n");

  // give other processes a chance to run including the devfs process
  // which wll populate devices we will need shortly.
  // TODO: do this in a more robust way
  alarm_sleep_ms(50);

  ls("/");
  ls("/dev");

  launch_init_process();
  sched_again(SCHED_BLOCKED);
  unreachable;
}

_used void ap_main() {
  QEMU_DEBUG_CHARP("ap_main\n");
  cpu_early_init();
  do_percpu_early_initializers();
  kprintf("initializing\n");

  // the BSP has pre-allocated for us a main thread and address space to avoid
  // lock contention on wait locks before we can initialize the scheduler. the
  // only setup needed before we can use the memory subsystem is to attach our
  // main thread to proc0.
  proc0_ap_init();

  // initialize the scheduler as soon as possible because running with multiple
  // cpus can hit contention on any wait lock which results in a context switch
  sched_init();

  // now we can run the late stage cpu and any percpu static initializers
  cpu_late_init();
  do_percpu_static_initializers();

  kprintf("done!\n");
  sched_again(SCHED_BLOCKED);
  unreachable;
}

//

void init_process_alloc_strings(char **out_path, char ***out_args, char ***out_env);
void init_process_free_strings(char *path, char **args, char **env);

void launch_init_process() {
#define FAIL_IF_ERROR(msg, ...) \
  if (res < 0) {                \
    kprintf("launch_init_process: " msg "\n", ##__VA_ARGS__); \
    goto fail;                  \
  }

  char *init_path = NULL;
  char **init_args = NULL;
  char **init_env = NULL;
  init_process_alloc_strings(&init_path, &init_args, &init_env);

  int res;
  kprintf("launching init process\n");
  proc_t *init_proc = proc_alloc_new(getref(curproc->creds));
  proc_setup_add_thread(init_proc, thread_alloc(0, SIZE_16KB));

  res = proc_setup_exec_args(init_proc, init_args);
  FAIL_IF_ERROR("proc_setup_exec_args failed: {:err}", res);
  res = proc_setup_exec_env(init_proc, init_env);
  FAIL_IF_ERROR("proc_setup_exec_env failed: {:err}", res);
  res = proc_setup_exec(init_proc, cstr_make(init_path));
  FAIL_IF_ERROR("proc_setup_exec failed: {:err}", res);
  res = proc_setup_open_fd(init_proc, 0, cstr_make("/dev/null"), O_RDONLY);
  FAIL_IF_ERROR("proc_setup_open_fd 0 failed: {:err}", res);
  res = proc_setup_open_fd(init_proc, 1, cstr_make("/dev/debug"), O_RDWR|O_NOCTTY);
  FAIL_IF_ERROR("proc_setup_open_fd 1 failed: {:err}", res);
  res = proc_setup_open_fd(init_proc, 2, cstr_make("/dev/debug"), O_RDWR|O_NOCTTY);
  FAIL_IF_ERROR("proc_setup_open_fd 2 failed: {:err}", res);
  proc_finish_setup_and_submit_all(moveref(init_proc));
  init_process_free_strings(init_path, init_args, init_env);
  return;
LABEL(fail);
  kprintf("failed to launch init process\n");
  init_process_free_strings(init_path, init_args, init_env);
  pr_putref(&init_proc);
#undef FAIL_IF_ERROR
}

void init_process_alloc_strings(char **out_path, char ***out_args, char ***out_env) {
  cstr_t init_path = cstr_from_str(init_program);
  if (cstr_eq(init_path, cstr_null)) {
    init_path = cstr_make("/sbin/init");
  }
  cstr_t shell_path = cstr_from_str(init_shell_program);
  if (cstr_eq(shell_path, cstr_null)) {
    shell_path = cstr_make("/sbin/shell");
  }
  cstr_t tty_dev_path = cstr_from_str(init_tty_device);
  if (cstr_eq(tty_dev_path, cstr_null)) {
    tty_dev_path = cstr_make("/dev/ttyS0");
  }

  // allocate a string for the path, and NULL-terminated arrays for argp and envp
  char *path = kasprintf("{:cstr}", &init_path);
  kassert(path != NULL && "failed to allocate path for init process");

  char **args = kmallocz(sizeof(const char *) * 2);
  kassert(args != NULL && "failed to allocate args for init process");
  args[0] = path;

  char **env = kmallocz(sizeof(const char *) * 3);
  kassert(env != NULL && "failed to allocate env for init process");
  env[0] = kasprintf("SHELL={:cstr}", &shell_path);
  env[1] = kasprintf("TTY={:cstr}", &tty_dev_path);

  *out_path = path;
  *out_args = args;
  *out_env = env;

  // print out the values we are using
  kprintf("launch init process:\n");
  kprintf("  path: %s\n", path);
  kprintf("  args:\n");
  for (size_t i = 0; args[i] != NULL; i++) {
    kprintf("    args[%zu]: %s\n", i, args[i]);
  }
  kprintf("  env:\n");
  for (size_t i = 0; env[i] != NULL; i++) {
    kprintf("    env[%zu]: %s\n", i, env[i]);
  }
}

void init_process_free_strings(char *path, char **args, char **env) {
  kfree(path);

  // free each string in args
  for (int i = 0; args[i] != NULL; i++) {
    if (args[i] == path) {
      continue;
    }
    kfree(args[i]);
  }
  kfree(args);

  // free each string in env
  for (int i = 0; env[i] != NULL; i++) {
    kfree(env[i]);
  }
  kfree(env);
}
