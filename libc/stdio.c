//
// Created by Aaron Gill-Braun on 2019-04-21.
//

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <drivers/screen.h>
#include <drivers/serial.h>
#include <kernel/mem/heap.h>
#include <stdbool.h>
#include "math.h"

typedef enum {
  START,
  FLAGS,
  WIDTH,
  PRECISION,
  LENGTH,
  FORMAT,
} parse_state_t;

typedef enum {
  L_NONE,
  L_CHAR,
  L_SHORT,
  L_LONG,
  L_LONGLONG,
  L_LONGDOUBLE,
  L_INTMAX,
  L_SIZE,
} fmt_length_t;

typedef struct {
  // Flags
  uint16_t alt_form : 1;  // Use the alternate for numbers
  uint16_t pad_zero : 1;  // Pad with zeros instead of spaces
  uint16_t pad_right : 1; // Padding is applied to the right
  uint16_t add_space : 1; // Add a space if there is no sign
  uint16_t add_plus : 1;  // Add plus sign if positive number

  uint16_t is_unsigned : 1;  // Value is unsigned
  uint16_t is_uppercase : 1; // Use uppercase for letters
  uint16_t is_width_arg : 1; // Width is an argument index
  uint16_t is_prec_arg : 1;  // Precision is an argument index

  // Length
  fmt_length_t length; // Length of argument

  // Options
  int width;     // Width of the value
  int precision; // Precision of the value
} fmt_options_t;

char alt_form[2];
char prefix[16];
char number[64];

//
//
//

char digit2char(int d, int r) {
  switch (r) {
    case 2:
    case 10:
      return d + '0';
    case 16: {
      if (d <= 9)
        return d + '0';
      else
        return d + '7';
    }
    default:
      return d;
  }
}

int char2digit(char c, int r) {
  switch (r) {
    case 2:
    case 10:
      return c - '0'; // c - 48
    case 16:
      if (c >= 'a' && c <= 'f') {
        return c - 'W'; // c - 87
      } else if (c >= 'A' && c <= 'F') {
        return c - '7'; // c - 55
      } else {
        return c -  '0'; // c - 48
      }
    default:
      return -1;
  }
}

//

int _itoa(int value, char *str, int base, fmt_options_t *opts) {
  const char *lookup = opts->is_uppercase ? "0123456789abcdef" : "0123456789ABCDEF";

  int alt_form_len = 0;
  int prefix_len = 0;
  int padding_len = 0;
  int number_len = 0;


  if (opts->is_unsigned) {
    // unsigned number
    unsigned uvalue = (unsigned)value;
    int index = 63;
    if (uvalue == 0) {
      number[index] = '0';
      index--;
      number_len++;
    } else {
      while (uvalue != 0) {
        number[index] = lookup[uvalue % base];
        uvalue /= base;
        index--;
        number_len++;
      }
    }

    // alternate form
    if (opts->alt_form) {
      if (base == 2) {
        alt_form[0] = '0';
        alt_form[1] = 'b';
        alt_form_len = 2;
      } else if (base == 16) {
        alt_form[0] = '0';
        alt_form[1] = opts->is_uppercase ? 'X' : 'x';
        alt_form_len = 2;
      }
    }

    // calculate padding
    int total_len = opts->width - (number_len + alt_form_len);
    if (opts->is_width_arg) {
      // TODO: this
    } else {
      padding_len = imax(total_len, 0);
    }
  } else {
    // signed number
    int svalue = abs(value);
    int index = 63;
    if (svalue == 0) {
      number[index] = '0';
      index--;
      number_len++;
    } else {
      while (svalue != 0) {
        number[index] = lookup[svalue % base];
        svalue /= base;
        index--;
        number_len++;
      }
    }

    // prefix options
    if (value < 0) {
      prefix[0] = '-';
      prefix_len = 1;
    } else if (opts->add_plus) {
      prefix[0] = '+';
      prefix_len = 1;
    } else if (opts->add_space) {
      prefix[0] = ' ';
      prefix_len = 1;
    }

    // calculate padding
    int total_len = opts->width - number_len + prefix_len +
                    (opts->add_space || opts->add_plus);
    if (opts->is_width_arg) {
      // TODO: this
    } else {
      padding_len = imax(total_len, 0);
    }
  }

  // padding (a) - prefix - alt_form - padding (b) - number - padding (c)
  int index = 0;

  // padding
  if (!opts->pad_right && (opts->precision || !opts->pad_zero)) {
    for (int i = 0; i < padding_len; i++) {
      str[index] = ' ';
      index++;
    }
  }

  // prefix
  memcpy(str + index, prefix, prefix_len);
  index += prefix_len;

  // alt_form
  memcpy(str + index, alt_form, alt_form_len);
  index += alt_form_len;

  // padding
  if (opts->pad_zero && !(opts->precision || opts->pad_right)) {
    for (int i = 0; i < padding_len; i++) {
      str[index] = '0';
      index++;
    }
  }

  // number
  int test = 0;
  int number_index = 64 - number_len;
  int *ptr = &number_index;
  memcpy(str + index, number + number_index, number_len);
  index += number_len;

  // padding
  if (opts->pad_right) {
    for (int i = 0; i < padding_len; i++) {
      str[index] = ' ';
      index++;
    }
  }

  return index;
}

void _dtoa(double value, char *str, fmt_options_t *opts) {

}

int _atoi(const char *str, int base) {
  int index = 0;
  size_t len = strlen(str);
  if (len == 0) return 0; // undefined

  bool negative = false;
  if (str[index] == '+') {
    // ignore
    index++;
  } else if (str[index] == '-') {
    negative = true;
    index++;
  }

  int value = 0;
  while (str[index]) {
    char ch = str[index];
    if (ch >= '0' && ch <= '9') {
      int column = len - (index + 1);
      int digit = char2digit(ch, 10);
      value += digit * pow(base, column);
    } else {
      return -1;
    }

    index++;
  }

  return negative ? -value : value;
}

//

int parse_int(char *dest, const char *str) {
  char const *ptr = str;
  int n = 0;
  while (*ptr) {
    char ch = *ptr;
    if (ch >= '0' && ch <= '9') {
      dest[n] = ch;
      n++;
      ptr++;
    } else {
      break;
    }
  }

  dest[n] = '\0';
  return n;
}

int ksnprintf_internal(char *str, size_t size, bool limit, const char *format, va_list *valist) {
  char const *fmt_ptr = format;
  char buffer[128];

  int n = 0;
  fmt_options_t fmt_options = {};
  parse_state_t state = START;

  while (*fmt_ptr) {
    int format_len = 0;

    char ch = *fmt_ptr;
    if (state == START) {
      if (ch == '%') {
        state = FLAGS;
        fmt_ptr++;
        continue;
      } else {
        buffer[0] = ch;
        format_len++;
      }
    } else if (state == FLAGS) {
      switch (ch) {
        case '#':
          fmt_options.alt_form = true;
          break;
        case '0':
          fmt_options.pad_zero = true;
          break;
        case '-':
          fmt_options.pad_right = true;
          break;
        case ' ':
          fmt_options.add_space = true;
          break;
        case '+':
          fmt_options.add_plus = true;
          break;
        default:
          state = WIDTH;
          continue;
      }

      fmt_ptr++;
      continue;
    } else if (state == WIDTH) {
      char temp[16];
      if (ch >= '1' && ch <= '9') {
        int count = parse_int(temp, fmt_ptr);
        fmt_ptr += count;
        fmt_options.width = _atoi(temp, 10);
      } else if (ch == '*') {
        fmt_ptr++;
        int count = parse_int(temp, fmt_ptr);
        fmt_ptr += count;
        if (count > 0 && *fmt_ptr == '$') {
          fmt_ptr++;
          fmt_options.is_width_arg = true;
          fmt_options.width = atoi(temp);
        }
      }

      state = PRECISION;
      continue;
    } else if (state == PRECISION) {
      char temp[16];
      if (ch == '.') {
        fmt_ptr++;
        ch = *fmt_ptr;
        if (ch >= '1' && ch <= '9') {
          int count = parse_int(temp, fmt_ptr);
          fmt_ptr += count;
          fmt_options.precision = atoi(temp);
        } else if (ch == '*') {
          fmt_ptr++;
          int count = parse_int(temp, fmt_ptr);
          fmt_ptr += count;
          if (count > 0 && *fmt_ptr == '$') {
            fmt_ptr++;
            fmt_options.is_prec_arg = true;
            fmt_options.precision = atoi(temp);
          }
        }
      }

      state = LENGTH;
      continue;
    } else if (state == LENGTH) {
      switch (ch) {
        case 'h':
          fmt_options.length = *(fmt_ptr + 1) == 'h' ? L_CHAR : L_SHORT;
          break;
        case 'l':
          fmt_options.length = *(fmt_ptr + 1) == 'l' ? L_LONGLONG : L_LONG;
          break;
        case 'L':
          fmt_options.length = L_LONGDOUBLE;
          break;
        case 'j':
          fmt_options.length = L_INTMAX;
          break;
        case 'z':
          fmt_options.length = L_SIZE;
          break;
        default:
          fmt_options.length = L_NONE;
          state = FORMAT;
          continue;
      }

      state = FORMAT;
      fmt_ptr++;
      continue;
    } else if (state == FORMAT) {
      switch (ch) {
        case 'd':
        case 'i': {
          int value = va_arg(*valist, int);
          format_len = _itoa(value, buffer, 10, &fmt_options);
          break;
        }
        case 'b':
        case 'o':
        case 'u':
        case 'p':
        case 'x':
        case 'X': {
          if (ch == 'p') {
            // %#x
            fmt_options.alt_form = true;
            ch = 'x';
          } else if (ch == 'X') {
            fmt_options.is_uppercase = true;
          }
          fmt_options.is_unsigned = true;

          int base = ch == 'b' ? 2 :
                     ch == 'o' ? 8 :
                     ch == 'u' ? 10 :
                     ch == 'x' ? 16 :
                     ch == 'X' ? 16 :
                     10;

          unsigned value = va_arg(*valist, unsigned);
          format_len = _itoa(value, buffer, base, &fmt_options);
          break;
        }
        case 'e':
        case 'E':
          // scientific notation
          break;
        case 'f':
        case 'F':
          // double (in decimal notation)
          break;
        case 'g':
        case 'G':
          // double (in f/F or e/E notation)
          break;
        case 'a':
        case 'A':
          // double (in hex notation)
          break;
        case 'c': {
          char value = va_arg(*valist, int);
          buffer[0] = value;
          format_len = 1;
          break;
        }
        case 's': {
          char *value = va_arg(*valist, char *);
          int len = strlen(value);
          memcpy(buffer, value, len);
          format_len = len;
          break;
        }
        case 'n': {
          int *value = va_arg(*valist, int *);
          *value = n;
          break;
        }
        case 'm':
          // glibc extension - print output of strerror(errno) [no argument]
          break;
        case '%':
          buffer[0] = '%';
          format_len = 1;
          break;
        default:
          break;
      }
      state = START;
    }

    if (limit) {
      if (n + format_len > (int)size - 1) {
        size_t to_write = size - (n + format_len) - 1;
        memcpy(str + n, buffer, to_write);
        n += to_write;
        str[n] = '\0';
        return -1;
      }
    }

    memcpy(str + n, buffer, format_len);
    n += format_len;
    fmt_ptr++;
    memset(buffer, 0, format_len);
    memset(&fmt_options, 0, sizeof(fmt_options_t));
  }

  str[n] = '\0';
  return n + 1;
}

//
//
//

/*
 * ksnprintf - write formatted data to a sized buffer
 * ==================================================
 *
 * ksnprintf(char *str, size_t n, const char *format, ...);
 *
 */
int ksnprintf(char *str, size_t n, const char *format, ...) {
  va_list valist;
  va_start(valist, format);
  int vn = ksnprintf_internal(str, n, true, format, &valist);
  va_end(valist);
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
  int n = ksnprintf_internal(str, -1, false, format, &valist);
  va_end(valist);
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
  char str[256];
  va_list valist;
  va_start(valist, format);
  ksnprintf_internal(str, 256, true, format, &valist);
  va_end(valist);
  kputs(str);
  serial_write(COM1, str);
}
