//
// Created by Aaron Gill-Braun on 2021-03-07.
//

#include <kernel/printf.h>
#include <kernel/panic.h>
#include <kernel/mutex.h>
#include <kernel/clock.h>
#include <kernel/string.h>
#include <kernel/mm.h>
#include <kernel/fs.h>

#include <drivers/tty/uart.h>

#include <fmt/fmt.h>

#define BUFFER_SIZE 512

static void *impl_arg;
static void (*kprintf_lock_impl)(void *);
static void (*kprintf_unlock_impl)(void *);
static int (*kprintf_puts_impl)(void *, const char *);

static struct early_kprintf {
  mtx_t lock;
  uint16_t port;
  bool prefix;
} early_kprintf = {
  .port = COM2,
  .prefix = false,
};

static void early_kprintf_lock(void *arg) {
  struct early_kprintf *p = arg;
  mtx_spin_lock(&p->lock);
}

static void early_kprintf_unlock(void *arg) {
  struct early_kprintf *p = arg;
  mtx_spin_unlock(&p->lock);
}

static int early_kprintf_puts(void *arg, const char *s) {
  struct early_kprintf *p = arg;
  bool unlock = false;
  if (mtx_owner(&p->lock) != curthread) {
    mtx_spin_lock(&p->lock);
    unlock = true;
  }

  while (*s) {
    uart_hw_busy_write_ch(p->port, *s);
    s++;
  }

  if (unlock)
    mtx_spin_unlock(&p->lock);
  return 0;
}

//

void kprintf_early_init() {
  uart_hw_init(early_kprintf.port);
  impl_arg = &early_kprintf;
  mtx_init(&early_kprintf.lock, MTX_SPIN, "early_kprintf_lock");
  kprintf_lock_impl = early_kprintf_lock;
  kprintf_unlock_impl = early_kprintf_unlock;
  kprintf_puts_impl = early_kprintf_puts;
}

static void kprintf_static_init() {
  kprintf("kprintf: enabling log prefixes\n");
  early_kprintf.prefix = true;
}
STATIC_INIT(kprintf_static_init);

void kprintf_kputs(const char *str) {
  kprintf_puts_impl(impl_arg, str);
}

void kprintf_kputl(long val) {
  char str[12];
  if (ltostr_safe(val, str, 12) < 0) {
    kprintf_kputs("kprintf_kputl: invalid long value");
  } else {
    kprintf_puts_impl(impl_arg, str);
  }
}

// MARK: Public API

static size_t kprintf_fmt_format(const char *format, char *str, size_t n, int max_args, ...) {
  va_list valist;
  va_start(valist, max_args);
  size_t len = fmt_format(format, str, n, max_args, valist);
  va_end(valist);
  return len;
}

void kprintf(const char *format, ...) {
  __assert_stack_is_aligned();
  size_t len = strlen(format);
  if (len == 0) {
    return; // nothing to print
  }

  char str[BUFFER_SIZE];
  va_list valist;
  va_start(valist, format);
  fmt_format(format, str, BUFFER_SIZE, FMT_MAX_ARGS, valist);
  va_end(valist);

  if (early_kprintf.prefix && format[len-1] == '\n') {
    // treat as a log line and prepend the cpu and timestamp
    uint64_t nanos = clock_get_nanos();
    struct timeval time = timeval_from_nanos(nanos);
    char prefix_buf[64];
    size_t prefix_len = kprintf_fmt_format("[%-8lld.%06lld] CPU#%d: ", prefix_buf, sizeof(prefix_buf), FMT_MAX_ARGS,
                                           time.tv_sec, time.tv_usec, curcpu_id);
    kprintf_lock_impl(impl_arg);
    kprintf_puts_impl(impl_arg, prefix_buf);
    kprintf_puts_impl(impl_arg, str);
    kprintf_unlock_impl(impl_arg);
  } else {
    kprintf_puts_impl(impl_arg, str);
  }
}

void kprintf_raw(const char *format, ...) {
  char str[BUFFER_SIZE];
  va_list valist;
  va_start(valist, format);
  fmt_format(format, str, BUFFER_SIZE, FMT_MAX_ARGS, valist);
  va_end(valist);
  kprintf_puts_impl(impl_arg, str);
}

void kvfprintf(const char *format, va_list valist) {
  __assert_stack_is_aligned();
  char str[BUFFER_SIZE];
  fmt_format(format, str, BUFFER_SIZE, FMT_MAX_ARGS, valist);
  kprintf_puts_impl(impl_arg, str);
}

size_t ksprintf(char *str, const char *format, ...) {
  __assert_stack_is_aligned();
  va_list valist;
  va_start(valist, format);
  size_t n = fmt_format(format, str, INT32_MAX, FMT_MAX_ARGS, valist);
  kassert(n < INT32_MAX);
  va_end(valist);
  return (int) n;
}

size_t kvsprintf(char *str, const char *format, va_list valist) {
  __assert_stack_is_aligned();
  return fmt_format(format, str, INT32_MAX, FMT_MAX_ARGS, valist);
}

size_t ksnprintf(char *str, size_t n, const char *format, ...) {
  __assert_stack_is_aligned();
  va_list valist;
  va_start(valist, format);
  size_t vn = fmt_format(format, str, n, FMT_MAX_ARGS, valist);
  va_end(valist);
  return vn;
}

size_t kvsnprintf(char *str, size_t n, const char *format, va_list valist) {
  __assert_stack_is_aligned();
  return fmt_format(format, str, n, FMT_MAX_ARGS, valist);
}

char *kasprintf(const char *format, ...) {
  __assert_stack_is_aligned();
  char buffer[BUFFER_SIZE];

  va_list valist;
  va_start(valist, format);
  size_t n = fmt_format(format, buffer, BUFFER_SIZE, FMT_MAX_ARGS, valist);
  va_end(valist);

  char *str = kmalloc(n + 1);
  strcpy(str, buffer);
  return str;
}

char *kvasprintf(const char *format, va_list args) {
  __assert_stack_is_aligned();
  char buffer[BUFFER_SIZE];
  size_t n = fmt_format(format, buffer, BUFFER_SIZE, FMT_MAX_ARGS, args);
  char *str = kmalloc(n + 1);
  strcpy(str, buffer);
  return str;
}

int kfprintf(const char *path, const char *format, ...) {
  __assert_stack_is_aligned();
  int fd = fs_open(cstr_make(path), O_WRONLY, 0);
  if (fd < 0) {
    return fd;
  }

  char buffer[BUFFER_SIZE];
  va_list valist;
  va_start(valist, format);
  size_t n = fmt_format(format, buffer, BUFFER_SIZE, FMT_MAX_ARGS, valist);
  va_end(valist);

  kio_t kio = kio_new_readable(buffer, n);
  ssize_t res = fs_kwrite(fd, &kio);
  fs_close(fd);
  if (res < 0) {
    return (int)res;
  }
  return 0;
}

int kfdprintf(int fd, const char *format, ...) {
  __assert_stack_is_aligned();
  char buffer[BUFFER_SIZE];
  va_list valist;
  va_start(valist, format);
  size_t n = fmt_format(format, buffer, BUFFER_SIZE, FMT_MAX_ARGS, valist);
  va_end(valist);

  kio_t kio = kio_new_readable(buffer, n);
  ssize_t res = fs_kwrite(fd, &kio);
  if (res < 0) {
    return (int)res;
  }
  return 0;
}
