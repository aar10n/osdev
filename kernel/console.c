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
#include <mm.h>

#include <drivers/serial.h>
#include <gui/screen.h>
#include <format.h>

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

void kputsf(const char *format, ...) {
  char str[CMD_BUFFER_SIZE];
  va_list valist;
  va_start(valist, format);
  print_format(format, str, CMD_BUFFER_SIZE, valist, true);
  va_end(valist);
  DISPATCH(cmdline_console, puts, str);
}
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

//
//
// MARK: Command Line
// TODO: move into separate standalone binary
//

static ssize_t cmdline_get_input_line(char *buffer, size_t size) {
  kassert(size > 1);

  // collect input line
  int ch;
  size_t len = 0;
  while ((ch = kgetc()) > 0) {
    if (ch == '\b') {
      if (len > 0) {
        buffer[len--] = '\0';
        kputc(ch);
      }
      continue;
    } else if (ch == '\n') {
      buffer[len] = '\0';
      kputc('\n');
      break;
    }

    if (len < size - 2) {
      buffer[len++] = ch;
      kputc(ch);
    }
  }

  if (ch <= 0) {
    kprintf("cmdline: call to kgetc failed\n");
    return -1;
  }
  return len;
}

static char **cmdline_split_line(const char *line, size_t len, size_t *outlen) {
  if (len == 0) {
    return NULL;
  }

  // count delimiters
  size_t sep_count = 0;
  bool ignore_space = true;
  for (int i = 0; i < len; i++) {
    if (isspace(line[i])) {
      if (!ignore_space) {
        sep_count++;
        ignore_space = true;
      }
    } else {
      ignore_space = false;
    }
  }

  size_t array_size = sep_count + 2; // include NULL sentinel
  size_t array_len = 0;
  char **strings = kmalloc(sizeof(char *) * array_size);

  const char *ptr = line;
  const char *lptr = ptr;
  const char *eptr = line + len;

  // skip initial spaces
  while (ptr < eptr && isspace(*ptr))
    ptr++;

  while (ptr < eptr) {
    if (!isspace(*ptr)) {
      ptr++;
      continue;
    }

    // delimiter
    size_t slen = ptr - lptr;
    if (slen == 0) {
      ptr++;
      continue;
    }

    // add it to the array
    char *string = kmalloc(slen + 1);
    memcpy(string, lptr, slen);
    string[slen] = '\0';
    strings[array_len++] = string;
    ptr++;
    lptr = ptr;
  }

  // handle remainder of buffer
  size_t slen = ptr - lptr;
  if (slen > 0) {
    char *string = kmalloc(slen + 1);
    memcpy(string, lptr, slen);
    string[slen] = '\0';
    strings[array_len++] = string;
  }
  strings[array_size - 1] = NULL;

  if (outlen) {
    *outlen = array_len;
  }
  return strings;
}

static void cmdline_free_strings(char **strings) {
  char **ptr = strings;
  while (*ptr) {
    kfree(*ptr++);
  }
  kfree(strings);
}

// MARK: Console Commands

#include <thread.h>

static int cmdline_ls_command(const char **args, size_t args_len) {
  if (args_len == 0) {
    kputsf("error: ls <path>\n");
    return -1;
  }
  return -1;
  // kputsf("listing contents of %s\n", args[0]);
  //
  // const char *path = args[0];
  // int fd = fs_open(path, O_RDONLY | O_DIRECTORY, 0);
  // if (fd < 0) {
  //   kputsf("error: %s\n", strerror(ERRNO));
  //   return -1;
  // }
  //
  // dentry_t *dentry;
  // while ((dentry = fs_readdir(fd)) != NULL) {
  //   if (strcmp(dentry->name, ".") == 0 || strcmp(dentry->name, "..") == 0) {
  //     continue;
  //   }
  //
  //   if (IS_IFDIR(dentry->mode)) {
  //     kputsf("  %s/\n", dentry->name);
  //   } else {
  //     kputsf("  %s\n", dentry->name);
  //   }
  // }
  //
  // fs_close(fd);
  // return 0;
}

static int cmdline_mount_command(const char **args, size_t args_len) {
  if (args_len != 3) {
    kputsf("error: mount <device> <path> <format>\n");
    return -1;
  }

  const char *device = args[0];
  const char *path = args[1];
  const char *format = args[2];
  int result = fs_mount(path, device, format);
  if (result < 0) {
    kputsf("error: %s\n", strerror(ERRNO));
    return -1;
  }

  kputsf("ok\n");
  return 0;
}

// MARK: Console Main

static int cmdline_process_line(const char *buffer, size_t len) {
#define HANDLE_COMMAND(name, func) \
  if (strcmp(command, name) == 0) {\
    int result = func((const char **)(strings + 1), num_strings - 1); \
    cmdline_free_strings(strings); \
    return result; \
  }

  size_t num_strings;
  char **strings = cmdline_split_line(buffer, len, &num_strings);
  kassert(strings != NULL);
  kassert(num_strings > 0);

  const char *command = strings[0];
  HANDLE_COMMAND("ls", cmdline_ls_command);
  HANDLE_COMMAND("mount", cmdline_mount_command);

  kputsf("error: unknown command %s\n", command);
  cmdline_free_strings(strings);
  return 0;
}

int command_line_main() {
  kprintf("console: starting command line\n");
  char buffer[CMD_BUFFER_SIZE];

  while (true) {
    kputs("<kernel>: "); // prompt

    // collect input line
    ssize_t len = cmdline_get_input_line(buffer, CMD_BUFFER_SIZE);
    if (len < 0) {
      panic("failed to get cmdline input line");
    } else if (len == 0) {
      continue;
    }

    cmdline_process_line(buffer, len);
  }
}
