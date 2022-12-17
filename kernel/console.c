//
// Created by Aaron Gill-Braun on 2022-06-05.
//

#include <console.h>
#include <spinlock.h>
#include <mutex.h>
#include <chan.h>
#include <input.h>
#include <printf.h>
#include <panic.h>
#include <string.h>
#include <vector.h>
#include <mm.h>

#include <drivers/serial.h>
#include <gui/screen.h>

#define CMD_BUFFER_SIZE 256

#define DISPATCH(console, func, args...) \
  ({                                     \
    console_iface_t *iface = (console)->iface; \
    iface->func ? iface->func((void *)(console), ##args) : -1; \
  })

/**
 * Represents an I/O interface for consoles.
 * A console I/O inteface must implement at least `puts` and `putc`
 * whereas `getc` is optional. The data for the interface is passed
 * via the `ptr` argument. It is the responsibility of the interface
 * to ensure it is thread-safe.
 */
typedef struct console_iface {
  const char *name;
  int (*puts)(void *ptr, const char *s);
  int (*putc)(void *ptr, char c);
  int (*getc)(void *ptr);
} console_iface_t;

/**
 * The base fields for all console implementations.
 * Must be declared at the top of any console struct.
 */
#define CONSOLE_BASE struct { console_iface_t *iface; }


typedef struct console {
  CONSOLE_BASE;
} console_t;

console_t *cmdline_console;
console_t *debug_console;

// MARK: Kernel Console I/O

void kputs(const char *s) { DISPATCH(cmdline_console, puts, s); }
void kputc(char c) { DISPATCH(cmdline_console, putc, c); }
int kgetc() { return DISPATCH(cmdline_console, getc); }

void debug_kputs(const char *s) { DISPATCH(debug_console, puts, s); }
void debug_kputc(char c) { DISPATCH(debug_console, putc, c); }

//
// MARK: Console Implementations
//

// MARK: Early Console Interface

/// The early console is pretty much the very first thing initialized
/// when we boot the kernel. It writes to the serial port but does not
/// suport user input at all. For this reason is it only used for the
/// debug console.

struct early_console {
  CONSOLE_BASE;
  int port;
  spinlock_t lock;
};

int early_puts(void *ptr, const char *s) {
  struct early_console *con = ptr;
  spin_lock(&con->lock);
  serial_write(con->port, s);
  spin_unlock(&con->lock);
  return 0;
}

int early_putc(void *ptr, char c) {
  struct early_console *con = ptr;
  spin_lock(&con->lock);
  serial_write_char(con->port, c);
  spin_unlock(&con->lock);
  return 0;
}

//

static console_iface_t early_console_iface = {
  .name = "early_console",
  .puts = early_puts,
  .putc = early_putc,
  .getc = NULL,
};
static struct early_console early_console_impl = {
  .iface = &early_console_iface,
  .port = COM1,
  .lock = {0},
};

void console_early_init() {
  spin_init(&early_console_impl.lock);
  serial_init(early_console_impl.port);

  cmdline_console = (console_t *) &early_console_impl;
  debug_console = (console_t *) &early_console_impl;
}

// MARK: Cmdline Console

/// The command line console is initialized near the end of kernel
/// initialization during the module init phase. It supports outputting
/// text to the screen and serial port and receiving user input from the
/// keyboard.

struct cmdline_console {
  CONSOLE_BASE;
  int port;
  mutex_t lock;
};

int cmdline_puts(void *ptr, const char *s) {
  struct cmdline_console *con = ptr;
  mutex_lock(&con->lock);
  serial_write(con->port, s);
  screen_print_str(s);
  mutex_unlock(&con->lock);
  return 0;
}

int cmdline_putc(void *ptr, char c) {
  struct cmdline_console *con = ptr;
  mutex_lock(&con->lock);
  serial_write_char(con->port, c);
  screen_print_char(c);
  mutex_unlock(&con->lock);
  return 0;
}

int cmdline_getc(void *ptr) {
  struct cmdline_console *con = ptr;
  int result;
  input_key_event_t event;

  mutex_lock(&con->lock);
  while (true) {
    result = chan_recv(key_event_stream, &event.raw);
    if (result < 0) {
      break;
    }

    int ch = input_key_event_to_char(&event);
    if (ch != 0) {
      result = ch;
      break;
    }
    // wait for a printable key press
  }
  mutex_unlock(&con->lock);
  return result;
}

//

static console_iface_t cmdline_console_iface = {
  .name = "cmdline_console",
  .puts = cmdline_puts,
  .putc = cmdline_putc,
  .getc = cmdline_getc,
};
static struct cmdline_console cmdline_console_impl = {
  .iface = &cmdline_console_iface,
  .port = COM2,
  .lock = {0},
};

void console_cmdline_init() {
  mutex_init(&cmdline_console_impl.lock, 0);
  serial_init(cmdline_console_impl.port);
  cmdline_console = (console_t *) &cmdline_console_impl;
}
MODULE_INIT(console_cmdline_init);
