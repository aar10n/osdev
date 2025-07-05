//
// Created by Aaron Gill-Braun on 2025-06-01.
//

#include <kernel/console.h>
#include <kernel/input.h>
#include <kernel/params.h>
#include <kernel/proc.h>
#include <kernel/tty.h>
#include <kernel/sched.h>

#include <kernel/printf.h>
#include <kernel/panic.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("console: " fmt, ##__VA_ARGS__)
#define EPRINTF(fmt, ...) kprintf("console: %s: " fmt, __func__, ##__VA_ARGS__)

KERNEL_PARAM("console.driver", str_t, console_driver_param, str_null);
static console_t *active_console = NULL;
static LIST_HEAD(console_t) consoles = {0};


static int console_main() {
  if (!active_console) {
    DPRINTF("no active console specified, exiting\n");
    return 0;
  }

  // start the kernel console if specified
  tty_t *tty = active_console->tty;
  DPRINTF("starting '%s' kernel console\n", active_console->name);
  if (!tty_lock(tty)) {
    EPRINTF("tty device is gone\n");
    return -ENXIO;
  }

  int res;
  if ((res = tty_open(active_console->tty)) < 0) {
    EPRINTF("failed to open tty for console driver %s: {:err}\n", active_console->name, res);
    tty_unlock(tty);
    return res;
  }

  const char *prompt = "$ ";
  char line[LINE_MAX];
  while (true) {
    kio_t kio = kio_new_readable(prompt, 2);
    if (ttydisc_write(active_console->tty, &kio) < 0) {
      EPRINTF("failed to write prompt to console\n");
      break;
    }

    kio = kio_new_writable(line, LINE_MAX);
    size_t len = ttydisc_read(active_console->tty, &kio);
    DPRINTF("console input read: {:zu} bytes\n", len);
    if (len < 0) {
      EPRINTF("failed to read console input: {:err}\n", len);
      break;
    } else if (len == 0) {
      DPRINTF("console received EOF\n");
      ttydisc_write_ch(tty, '\n');
      continue;
    }

    if (line[0] == '\0' || line[len-1] != '\n') {
      // ensure a newline is added if one wasn't typed
      ttydisc_write_ch(tty, '\n');
      continue; // empty line, ignore
    } else if (line[len-1] == '\n') {
      len--; // remove trailing newline
    }

    cstr_t command = cstr_new(line, len);
    DPRINTF("console command: {:cstr}\n", &command);
  }

  if ((res = tty_close(active_console->tty)) < 0) {
    EPRINTF("failed to close tty for console driver %s: {:err}\n", &active_console->name, res);
  }
  tty_unlock(tty);

  DPRINTF("exiting console input processing\n");
  return 0;
}

//
// MARK: Console API
//

void console_register(console_t *console) {
  ASSERT(console != NULL);
  kprintf("registering console: %s\n", console->name);
  LIST_ADD_FRONT(&consoles, console, list);
}

void console_init() {
  // select the active console
  if (!str_isnull(console_driver_param)) {
    active_console = LIST_FIND(cons, &consoles, list, str_eq_charp(console_driver_param, cons->name));
    if (active_console != NULL) {
      DPRINTF("using console: %s\n", active_console->name);
    } else {
      EPRINTF("no console found matching {:str}, no active console\n", &console_driver_param);
      return;
    }
  } else {
    DPRINTF("no console specified, no active console\n");
    return;
  }

  // start the console in a new process
  __ref proc_t *console_proc = proc_alloc_new(getref(curproc->creds));
  proc_setup_add_thread(console_proc, thread_alloc(TDF_KTHREAD, SIZE_16KB));
  proc_setup_entry(console_proc, (uintptr_t) console_main, 0);
  proc_setup_name(console_proc, cstr_make("console"));
  proc_finish_setup_and_submit_all(moveref(console_proc));
}
