//
// Created by Aaron Gill-Braun on 2021-03-07.
//

#include <kernel/printf.h>
#include <kernel/console.h>
#include <kernel/panic.h>
#include <kernel/string.h>
#include <kernel/mm.h>

#include <fmt/fmt.h>

#define BUFFER_SIZE 512

// MARK: Public API

void kprintf(const char *format, ...) {
  char str[BUFFER_SIZE];
  va_list valist;
  va_start(valist, format);
  fmt_format(format, str, BUFFER_SIZE, FMT_MAX_ARGS, valist);
  va_end(valist);
  debug_kputs(str);
}

void kvfprintf(const char *format, va_list valist) {
  char str[BUFFER_SIZE];
  fmt_format(format, str, BUFFER_SIZE, FMT_MAX_ARGS, valist);
  debug_kputs(str);
}

/*
 * ksprintf - write formatted data to a buffer
 * ===========================================
 *
 * ksprintf(char *str, const char *format, ...);
 *
 */
size_t ksprintf(char *str, const char *format, ...) {
  va_list valist;
  va_start(valist, format);
  size_t n = fmt_format(format, str, INT32_MAX, FMT_MAX_ARGS, valist);
  kassert(n < INT32_MAX);
  va_end(valist);
  return (int) n;
}

size_t kvsprintf(char *str, const char *format, va_list valist) {
  return fmt_format(format, str, INT32_MAX, FMT_MAX_ARGS, valist);
}

/*
 * ksnprintf - write formatted data to a sized buffer
 * =================================================
 *
 * ksnprintf(char *str, size_t n, const char *format, ...);
 *
 */
size_t ksnprintf(char *str, size_t n, const char *format, ...) {
  va_list valist;
  va_start(valist, format);
  size_t vn = fmt_format(format, str, n, FMT_MAX_ARGS, valist);
  va_end(valist);
  return vn;
}

size_t kvsnprintf(char *str, size_t n, const char *format, va_list valist) {
  return fmt_format(format, str, n, FMT_MAX_ARGS, valist);
}

/*
 * kasprintf - write formatted data to an allocated string
 * =======================================================
 *
 * kasprintf(const char *format, ...);
 *
 * This does not support strings longer than 512 characters.
 * It is the callers responsibility to free the allocated
 * buffer.
 */
char *kasprintf(const char *format, ...) {
  char buffer[BUFFER_SIZE];

  va_list valist;
  va_start(valist, format);
  size_t n = fmt_format(format, buffer, BUFFER_SIZE, FMT_MAX_ARGS, valist);
  va_end(valist);

  char *str = kmalloc(n + 1);
  strcpy(str, buffer);
  return str;
}
