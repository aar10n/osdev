//
// Created by Aaron Gill-Braun on 2020-08-31.
//

#include <base.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <kernel/panic.h>

#include <drivers/serial.h>

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

  uint16_t is_signed : 1;    // Value is signed
  uint16_t is_uppercase : 1; // Use uppercase for letters
  uint16_t is_width_arg : 1; // Width is an argument index
  uint16_t is_prec_arg : 1;  // Precision is an argument index

  // Length
  fmt_length_t length; // Length of argument

  // Options
  uint32_t width;     // Width of the value
  uint32_t precision; // Precision of the value
} fmt_options_t;

typedef union double_raw {
  double value;
  struct {
    uint64_t frac : 52;
    uint64_t exp : 11;
    uint64_t sign : 1;
  };
} double_raw_t;

// Buffers
#define PRINTF_BUFFER_SIZE 1024

#define NTOA_BUFFER_SIZE 64
#define FTOA_BUFFER_SIZE 64

static const double pow10[] = {
  1, 10, 100, 1000, 10000, 100000,
  1000000, 10000000, 100000000, 1000000000
};

//

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

int ntoa_signed(char *buf, long long value, int base, fmt_options_t *opts) {
  const char *lookup = opts->is_uppercase ? "0123456789ABCDEF" : "0123456789abcdef";

  int index = 0;
  if (value == 0) {
    buf[index] = '0';
    return 1;
  } else {
    while (value != 0) {
      buf[index] = lookup[value % base];
      value /= base;
      index++;
    }

    return index;
  }
}

int ntoa_unsigned(char *buf, unsigned long long value, int base, fmt_options_t *opts) {
  const char *lookup = opts->is_uppercase ? "0123456789ABCDEF" : "0123456789abcdef";

  int index = 0;
  if (value == 0) {
    buf[index] = '0';
    return 1;
  } else {
    while (value != 0) {
      buf[index] = lookup[value % base];
      value /= base;
      index++;
    }
    return index;
  }
}

int apply_alt_form(char *buf, int base, fmt_options_t *opts) {
  if (opts->alt_form) {
    if (base == 2) {
      buf[0] = '0';
      buf[1] = 'b';
      return 2;
    } else if (base == 16) {
      buf[0] = '0';
      buf[1] = 'x';
      return 2;
    }
  }
  return 0;
}

int apply_prefix(char *buf, bool negative, fmt_options_t *opts) {
  if (negative) {
    buf[0] = '-';
    return 1;
  } else if (opts->add_plus) {
    buf[0] = '+';
    return 1;
  } else if (opts->add_space) {
    buf[0] = ' ';
    return 1;
  }
  return 0;
}

//


int _ntoa(char *buf, unsigned long long value, int base, fmt_options_t *opts) {
  int prefix_len = 0;
  int padding_len = 0;
  int number_len = 0;

  static char number[NTOA_BUFFER_SIZE];
  static char prefix[16];
  if (opts->is_signed) {
    // signed number
    number_len = ntoa_signed(number, abs((long long) value), base, opts);
    // prefix options
    prefix_len = apply_prefix(prefix, (long long) value < 0, opts);
  } else {
    // unsigned number
    number_len = ntoa_unsigned(number, value, base, opts);
    // alt form options
    prefix_len = apply_alt_form(prefix, base, opts);
  }

  // reverse the number
  number[number_len] = '\0';
  reverse(number);

  // calculate padding
  uint32_t total_len = opts->width - (number_len + prefix_len);
  padding_len = max(total_len, 0);

  // | space padding - prefix - zero padding - number - right padding |
  int index = 0;

  // space padding
  if (!opts->pad_right && (opts->precision || !opts->pad_zero)) {
    for (int i = 0; i < padding_len; i++) {
      buf[index] = ' ';
      index++;
    }
  }

  // number prefix
  memcpy(buf + index, prefix, prefix_len);
  index += prefix_len;

  // zero padding
  if (opts->pad_zero && !(opts->precision || opts->pad_right)) {
    for (int i = 0; i < padding_len; i++) {
      buf[index] = '0';
      index++;
    }
  }

  // number
  memcpy(buf + index, number, number_len);
  index += number_len;

  // right padding
  if (opts->pad_right) {
    for (int i = 0; i < padding_len; i++) {
      buf[index] = ' ';
      index++;
    }
  }

  return index;
}

int _ftoa(char *buf, double value, fmt_options_t *opts) {
  static char fnumber[FTOA_BUFFER_SIZE];
  static char fprefix[16];

  int prefix_len = 0;
  int padding_len = 0;
  int number_len = 0;

  double_raw_t raw = { .value = value };

  prefix_len = apply_prefix(fprefix, value < 0, opts);
  if (raw.exp == 0 && raw.frac == 0) {
    // signed zero
    fnumber[0] = '0';
    number_len = 1;
  } else if (raw.exp == 0x7FF && raw.frac == 0) {
    // infinity
    const char *inf = opts->is_uppercase ? "INFINITY" : "infinity";
    int len = strlen(inf);
    memcpy(fnumber, inf, len);
    number_len = len;
  } else if (raw.exp == 0x7FF && raw.frac != 0) {
    // NaN
    const char *nan = opts->is_uppercase ? "NAN" : "nan";
    int len = strlen(nan);
    memcpy(fnumber, nan, len);
    number_len = len;
  } else {
    double diff = 0.0;
    size_t len = 0;
    uint32_t prec = opts->precision;

    // set default precision, if not set explicitly
    if (!prec) {
      prec = 6;
    }
    // limit precision to 9, cause a prec >= 10 can lead to overflow errors
    while ((len < FTOA_BUFFER_SIZE) && (prec > 9)) {
      buf[len++] = '0';
      prec--;
    }

    int whole = (int) value;
    double tmp = (value - whole) * pow10[prec];
    unsigned long long frac = (unsigned long long) tmp;
    diff = tmp - (double) frac;

    if (diff > 0.5) {
      ++frac;
      // handle rollover, e.g. case 0.99 with prec 1 is 1.0
      if ((double) frac >= pow10[prec]) {
        frac = 0;
        ++whole;
      }
    } else if (diff < 0.5) {
    } else if ((frac == 0) || (frac & 1)) {
      // if halfway, round up if odd OR if last digit is 0
      ++frac;
    }

    unsigned int count = prec;
    // now do fractional part, as an unsigned number
    while (len < FTOA_BUFFER_SIZE) {
      --count;
      fnumber[len++] = (char) (48 + (frac % 10));
      if (!(frac /= 10)) {
        break;
      }
    }

    // add extra 0s
    while ((len < FTOA_BUFFER_SIZE) && (count-- > 0U)) {
      fnumber[len++] = '0';
    }

    if (len < FTOA_BUFFER_SIZE) {
      // add decimal
      fnumber[len++] = '.';
    }

    // do whole part, number is reversed
    while (len < FTOA_BUFFER_SIZE) {
      fnumber[len++] = (char)(48 + (whole % 10));
      if (!(whole /= 10)) {
        break;
      }
    }

    unsigned width = opts->width;
    // pad leading zeros
    if (!opts->pad_right && opts->pad_zero) {
      if (opts->width && (value < 0 || (opts->add_plus || opts->add_space))) {
        width--;
      }
      while ((len < width) && (len < FTOA_BUFFER_SIZE)) {
        fnumber[len++] = '0';
      }
    }

    number_len = len;
  }

  fnumber[number_len] = '\0';
  reverse(fnumber);

  int index = 0;

  // prefix
  memcpy(buf + index, fprefix, prefix_len);
  index += prefix_len;

  // number
  memcpy(buf + index, fnumber, number_len);
  index += number_len;

  return index;
}

//

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
      int column = (int) len - (index + 1);
      int digit = char2digit(ch, 10);
      value += digit * (int) pow(base, column);
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

int ksnprintf_internal(char *str, size_t size, bool limit, const char *format, va_list args) {
  va_list valist;
  va_copy(valist, args);

  char const *fmt_ptr = format;
  static char buffer[128];

  int n = 0;
  fmt_options_t opts = {};
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
          opts.alt_form = true;
          break;
        case '0':
          opts.pad_zero = true;
          break;
        case '-':
          opts.pad_right = true;
          break;
        case ' ':
          opts.add_space = true;
          break;
        case '+':
          opts.add_plus = true;
          break;
        default:
          state = WIDTH;
          continue;
      }

      fmt_ptr++;
      continue;
    } else if (state == WIDTH) {
      static char width[4];
      if (ch >= '1' && ch <= '9') {
        int count = parse_int(width, fmt_ptr);
        fmt_ptr += count;
        opts.width = _atoi(width, 10);
      } else if (ch == '*') {
        fmt_ptr++;
        int count = parse_int(width, fmt_ptr);
        fmt_ptr += count;
        if (count > 0 && *fmt_ptr == '$') {
          fmt_ptr++;
          opts.is_width_arg = true;
          opts.width = atoi(width);
        }
      }

      state = PRECISION;
      continue;
    } else if (state == PRECISION) {
      static char prec[4];
      if (ch == '.') {
        fmt_ptr++;
        ch = *fmt_ptr;
        if (ch >= '1' && ch <= '9') {
          int count = parse_int(prec, fmt_ptr);
          fmt_ptr += count;
          opts.precision = atoi(prec);
        } else if (ch == '*') {
          fmt_ptr++;
          int count = parse_int(prec, fmt_ptr);
          fmt_ptr += count;
          if (count > 0 && *fmt_ptr == '$') {
            fmt_ptr++;
            opts.is_prec_arg = true;
            opts.precision = atoi(prec);
          }
        }
      }

      state = LENGTH;
      continue;
    } else if (state == LENGTH) {
      switch (ch) {
        case 'h': {
          if (*(fmt_ptr + 1) == 'h') {
            opts.length = L_CHAR;
            fmt_ptr++;
          } else {
            opts.length = L_SHORT;
          }
          break;
        }
        case 'l': {
          if (*(fmt_ptr + 1) == 'l') {
            opts.length = L_LONGLONG;
            fmt_ptr++;
          } else {
            opts.length = L_LONG;
          }
          break;

        }
        case 'L':
          opts.length = L_LONGDOUBLE;
          break;
        case 'j':
          opts.length = L_INTMAX;
          break;
        case 'z':
          opts.length = L_SIZE;
          break;
        default:
          opts.length = L_NONE;
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
          opts.is_signed = true;

          long long int value;
          if (opts.length == L_LONGLONG) {
            value = va_arg(valist, long long int);
          } else {
            value = va_arg(valist, int);
          }
          format_len = _ntoa(buffer, value, 10, &opts);
          break;
        }
        case 'b':
        case 'o':
        case 'u':
        case 'p':
        case 'x':
        case 'X': {
          if (ch == 'p') {
            opts.alt_form = true;
            opts.length = L_LONGLONG;
            ch = 'x';
          } else if (ch == 'X') {
            opts.is_uppercase = true;
          }

          int base = ch == 'b' ? 2 :
                     ch == 'o' ? 8 :
                     ch == 'u' ? 10 :
                     ch == 'x' ? 16 :
                     ch == 'X' ? 16 :
                     10;

          unsigned long long value;
          if (opts.length == L_LONGLONG) {
            value = va_arg(valist, unsigned long long);
          } else {
            value = va_arg(valist, unsigned);
          }
          format_len = _ntoa(buffer, value, base, &opts);
          break;
        }
        case 'e':
        case 'E':
          // scientific notation
          break;
        case 'f':
        case 'F': {
          if (ch == 'F') {
            opts.is_uppercase = true;
          }

          // double (in decimal notation)
          double value = va_arg(valist, double);
          format_len = _ftoa(buffer, value, &opts);
          break;
        }
        case 'g':
        case 'G':
          // double (in f/F or e/E notation)
        case 'a':
        case 'A':
          // double (in hex notation)
          panic("Format specifier '%c' not supported\n", ch);
          break;
        case 'c': {
          char value = va_arg(valist, int);
          buffer[0] = value;
          format_len = 1;
          break;
        }
        case 's': {
          char *value = va_arg(valist, char *);
          int len = strlen(value);
          memcpy(buffer, value, len);
          format_len = len;
          break;
        }
        case 'n': {
          int *value = va_arg(valist, int *);
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
    memset(&opts, 0, sizeof(fmt_options_t));
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
  int vn = ksnprintf_internal(str, n, true, format, valist);
  va_end(valist);
  return vn;
}

int kvsnprintf(char *str, size_t n, const char *format, va_list args) {
  int vn = ksnprintf_internal(str, n, true, format, args);
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
  int n = ksnprintf_internal(str, -1, false, format, valist);
  va_end(valist);
  return n;
}

int kvsprintf(char *str, const char *format, va_list args) {
  int n = ksnprintf_internal(str, -1, false, format, args);
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
  static char str[PRINTF_BUFFER_SIZE];
  va_list valist;
  va_start(valist, format);
  ksnprintf_internal(str, PRINTF_BUFFER_SIZE, true, format, valist);
  va_end(valist);
  // kputs(str);
  serial_write(COM1, str);
}

void kvfprintf(const char *format, va_list args) {
  static char str[PRINTF_BUFFER_SIZE];
  ksnprintf_internal(str, PRINTF_BUFFER_SIZE, true, format, args);
  // kputs(str);
  serial_write(COM1, str);
}
