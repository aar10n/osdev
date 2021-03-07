//
// Created by Aaron Gill-Braun on 2021-03-07.
//

#include <printf.h>
#include <format.h>
#include <lock.h>
#include <drivers/serial.h>

#define BUFFER_SIZE 512

static spinlock_t lock = {
  .locked = 0,
  .lock_count = 0,
  .locked_by = 0,
  .rflags = 0,
};

/*
 * snprintf - write formatted data to a sized buffer
 * =================================================
 *
 * ksnprintf(char *str, size_t n, const char *format, ...);
 *
 */
int ksnprintf(char *str, size_t n, const char *format, ...) {
  va_list valist;
  va_start(valist, format);
  int vn = print_format(format, str, n, valist, true);
  va_end(valist);
  return vn;
}

int kvsnprintf(char *str, size_t n, const char *format, va_list valist) {
  int vn = print_format(format, str, n, valist, true);
  return vn;
}

/*
 * ksprintf - write formatted data to a buffer
 * ===========================================
 *
 * ksprintf(char *str, const char *format, ...);
 *
 */
int ksprintf(char *str, const char *format, ...) {
  va_list valist;
  va_start(valist, format);
  int n = print_format(format, str, -1, valist, false);
  va_end(valist);
  return n;
}

int kvsprintf(char *str, const char *format, va_list valist) {
  int n = print_format(format, str, -1, valist, false);
  return n;
}

/*
 * kprintf - write formatted data to standard output
 * =================================================
 *
 * kprintf(const char *format, ...);
 *
 * format: "%[flags][width][precision][length]<type>"
 *
 *
 * Flags
 *   '#' - Use alternate form for value. For 'x' and 'X' formatting,
 *         append "0x" to the value. For 'b' formatting, append "0b"
 *         to the value. For 'o' formatting, append "0" to the value.
 *   '0' - The value should be zero padded. If a width value is
 *         specified, pad with zeros instead of spaces
 *   '-' - Pad the value from the right side (default left)
 *   ' ' - If no signed is printed, insert a space before the value.
 *   '+' - Force add the '+' sign in front of positive numbers.
 *
 * Field Width
 *   An optional number specifying the minimum width of the
 *   converted value. If the converted value is smaller than
 *   the given width, it will be padded with spaces, or zeros
 *   if the '0' flag is used. By default the padding is from
 *   the left side, but can be changed with the '-' flag
 *
 * Precision
 * =========
 *
 * Field Length
 * ============
 *   'hh' - A char or unsigned char
 *   'h'  - A short int or unsigned short int
 *   'l'  - A long int or unsigned long int
 *   'll' - A long long int or unsigned long long int
 *   'z'  - A size_t or ssize_t
 *
 * Type Specifier
 * ==============
 *   'd' - Decimal
 *   'i' - Decimal
 *   'b' - Binary
 *   'o' - Octal
 *   'u' - Unsigned decimal
 *   'x' - Hexadecimal (lowercase)
 *   'X' - Hexadecimal (uppercase)
 *   'e' - Scientific notation (lowercase)
 *   'E' - Scientific notation (uppercase)
 *   'f' - Floating point (lowercase)
 *   'F' - Floating point (uppercase)
 *   'c' - Character
 *   's' - String
 *   'p' - Pointer address
 *   '%' - A '%' literal
 */
void kprintf(const char *format, ...) {
  char str[BUFFER_SIZE];
  va_list valist;
  va_start(valist, format);
  print_format(format, str, BUFFER_SIZE, valist, true);
  va_end(valist);

  lock(lock);
  serial_write(COM1, str);
  unlock(lock);
}

void kvfprintf(const char *format, va_list valist) {
  char str[BUFFER_SIZE];
  print_format(format, str, BUFFER_SIZE, valist, true);

  lock(lock);
  serial_write(COM1, str);
  unlock(lock);
}


__used void stdio_lock() {
  lock(lock);
}

__used void stdio_unlock() {
  unlock(lock);
}

