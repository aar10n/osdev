//
// Copyright (c) Aaron Gill-Braun. All rights reserved.
// Distributed under the terms of the MIT License. See LICENSE for details.
//

#include "fmtlib.h"
#include "fmt.h"

#include <kernel/mm.h>
#include <kernel/errno.h>
#include <kernel/time.h>
#include <kernel/string.h>
#include <kernel/str.h>
#include <kernel/vfs_types.h>
#include <kernel/vfs/path.h>
#include <kernel/vfs/file.h>
#include <kernel/lock.h>
#include <kernel/mutex.h>
#include <kernel/proc.h>

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

// using a precision over 9 can lead to overflow errors
#define PRECISION_DEFAULT 6
#define PRECISION_MAX 9
#define TEMP_BUFFER_SIZE (FMTLIB_MAX_WIDTH + 1)

#define mk_fmtlib_spec_u64(v, w) ((fmt_spec_t) { \
  .fill_char = '0',                           \
  .width = (w),                               \
  .value = fmt_rawvalue_uint64(v),            \
  .argtype = FMT_ARGTYPE_INT64,               \
})

#define mk_fmtlib_spec_pointer(p) ((fmt_spec_t) { \
  .flags = FMT_FLAG_ALT,                          \
  .fill_char = '0',                               \
  .width = 16,                                    \
  .value = fmt_rawvalue_voidptr(p),                 \
  .argtype = FMT_ARGTYPE_VOIDPTR,                 \
})

#define mk_fmtlib_spec_voidp(v) ((fmt_spec_t) { \
  .value = fmt_rawvalue_voidptr(v),             \
  .argtype = FMT_ARGTYPE_VOIDPTR,               \
})

typedef struct fmt_format_type {
  const char *type;
  fmt_formatter_t fn;
  fmt_argtype_t argtype;
} fmt_format_type_t;

union double_raw {
  double value;
  struct {
    uint64_t frac : 52;
    uint64_t exp : 11;
    uint64_t sign : 1;
  };
};

struct num_format {
  int base;
  const char *digits;
  const char *prefix;
};

static const struct num_format binary_format = { .base = 2, .digits = "01", .prefix = "0b" };
static const struct num_format octal_format = { .base = 8, .digits = "01234567", .prefix = "0o" };
static const struct num_format decimal_format = { .base = 10, .digits = "0123456789", .prefix = "" };
static const struct num_format hex_lower_format = { .base = 16, .digits = "0123456789abcdef", .prefix = "0x" };
static const struct num_format hex_upper_format = { .base = 16, .digits = "0123456789ABCDEF", .prefix = "0X" };

static const double pow10[] = { 1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000 };

static inline size_t u64_to_str(uint64_t value, char *buffer, const struct num_format *format) {
  size_t n = 0;
  int base = format->base;
  const char *digits = format->digits;
  if (value == 0) {
    buffer[n++] = '0';
    return n;
  }

  while (value > 0) {
    buffer[n++] = digits[value % base];
    value /= base;
  }

  // reverse buffer
  for (size_t i = 0; i < n / 2; i++) {
    char tmp = buffer[i];
    buffer[i] = buffer[n - i - 1];
    buffer[n - i - 1] = tmp;
  }
  return n;
}

// Forward declarations
static size_t format_double(fmt_buffer_t *buffer, const fmt_spec_t *spec);
static size_t format_signed(fmt_buffer_t *buffer, const fmt_spec_t *spec);
static size_t format_unsigned(fmt_buffer_t *buffer, const fmt_spec_t *spec);
static size_t format_octal(fmt_buffer_t *buffer, const fmt_spec_t *spec);
static size_t format_hex(fmt_buffer_t *buffer, const fmt_spec_t *spec);

// Scientific notation formatter
static size_t format_scientific(fmt_buffer_t *buffer, const fmt_spec_t *spec) {
  union double_raw v = { spec->value.double_value };
  size_t prec = (size_t) min((spec->precision < 0 ? PRECISION_DEFAULT : spec->precision), PRECISION_MAX);
  size_t n = 0;
  bool uppercase = (spec->flags & FMT_FLAG_UPPER) != 0;

  if (v.sign) {
    n += fmtlib_buffer_write_char(buffer, '-');
  } else if (spec->flags & FMT_FLAG_SIGN) {
    n += fmtlib_buffer_write_char(buffer, '+');
  } else if (spec->flags & FMT_FLAG_SPACE) {
    n += fmtlib_buffer_write_char(buffer, ' ');
  }

  if (v.exp == 0x7FF && v.frac == 0) {
    const char *inf = uppercase ? "INF" : "inf";
    n += fmtlib_buffer_write(buffer, inf, 3);
    return n;
  } else if (v.exp == 0x7FF && v.frac != 0) {
    const char *nan = uppercase ? "NAN" : "nan";
    n += fmtlib_buffer_write(buffer, nan, 3);
    return n;
  }

  double val = v.value;
  if (val < 0) val = -val;

  if (val == 0.0) {
    n += fmtlib_buffer_write_char(buffer, '0');
    if (prec > 0 || (spec->flags & FMT_FLAG_ALT)) {
      n += fmtlib_buffer_write_char(buffer, '.');
      for (size_t i = 0; i < prec; i++) {
        n += fmtlib_buffer_write_char(buffer, '0');
      }
    }
    n += fmtlib_buffer_write(buffer, uppercase ? "E+00" : "e+00", 4);
    return n;
  }

  int exp = 0;
  while (val >= 10.0) { val /= 10.0; exp++; }
  while (val < 1.0) { val *= 10.0; exp--; }

  uint64_t whole = (uint64_t) val;
  double tmp = (val - (double)whole) * pow10[prec];
  uint64_t frac = (uint64_t) tmp;

  double delta = tmp - (double)frac;
  if (delta > 0.5) {
    frac++;
    if (frac >= (uint64_t)pow10[prec]) {
      frac = 0;
      whole++;
      if (whole >= 10) {
        whole = 1;
        exp++;
      }
    }
  } else if (delta == 0.5 && ((frac == 0) || (frac & 1))) {
    frac++;
    if (frac >= (uint64_t)pow10[prec]) {
      frac = 0;
      whole++;
      if (whole >= 10) {
        whole = 1;
        exp++;
      }
    }
  }

  char temp[32];
  size_t len = u64_to_str(whole, temp, &decimal_format);
  n += fmtlib_buffer_write(buffer, temp, len);

  if (prec > 0 || (spec->flags & FMT_FLAG_ALT)) {
    n += fmtlib_buffer_write_char(buffer, '.');
    len = u64_to_str(frac, temp, &decimal_format);
    for (size_t i = len; i < prec; i++) {
      n += fmtlib_buffer_write_char(buffer, '0');
    }
    n += fmtlib_buffer_write(buffer, temp, len);
  }

  n += fmtlib_buffer_write_char(buffer, uppercase ? 'E' : 'e');
  n += fmtlib_buffer_write_char(buffer, exp >= 0 ? '+' : '-');
  if (exp < 0) exp = -exp;
  if (exp < 10) n += fmtlib_buffer_write_char(buffer, '0');
  len = u64_to_str(exp, temp, &decimal_format);
  n += fmtlib_buffer_write(buffer, temp, len);

  return n;
}

static size_t trim_trailing_zeros(char *str, size_t len, bool keep_decimal) {
  if (len == 0) return len;

  size_t decimal_pos = len;
  for (size_t i = 0; i < len; i++) {
    if (str[i] == '.') {
      decimal_pos = i;
      break;
    }
  }

  if (decimal_pos == len) return len;

  size_t new_len = len;
  while (new_len > decimal_pos + 1 && str[new_len - 1] == '0') {
    new_len--;
  }

  if (!keep_decimal && new_len == decimal_pos + 1) {
    new_len--;
  }

  return new_len;
}

static size_t format_general(fmt_buffer_t *buffer, const fmt_spec_t *spec) {
  union double_raw v = { .value = spec->value.double_value };
  size_t n = 0;
  bool uppercase = (spec->flags & FMT_FLAG_UPPER) != 0;

  if (v.sign) {
    n += fmtlib_buffer_write_char(buffer, '-');
  } else if (spec->flags & FMT_FLAG_SIGN) {
    n += fmtlib_buffer_write_char(buffer, '+');
  } else if (spec->flags & FMT_FLAG_SPACE) {
    n += fmtlib_buffer_write_char(buffer, ' ');
  }

  if (v.exp == 0x7FF && v.frac == 0) {
    const char *inf = uppercase ? "INF" : "inf";
    n += fmtlib_buffer_write(buffer, inf, 3);
    return n;
  } else if (v.exp == 0x7FF && v.frac != 0) {
    const char *nan = uppercase ? "NAN" : "nan";
    n += fmtlib_buffer_write(buffer, nan, 3);
    return n;
  }

  double val = v.value;
  if (val < 0) val = -val;

  int prec = spec->precision < 0 ? PRECISION_DEFAULT : spec->precision;
  if (prec == 0) prec = 1;

  int exp = 0;
  if (val != 0.0) {
    double temp = val;
    while (temp >= 10.0) { temp /= 10.0; exp++; }
    while (temp < 1.0) { temp *= 10.0; exp--; }
  }

  char temp_buf[256];
  fmt_buffer_t temp_buffer = fmtlib_buffer(temp_buf, sizeof(temp_buf));
  size_t formatted_len = 0;

  if (exp < -4 || exp >= prec) {
    fmt_spec_t sci_spec = *spec;
    sci_spec.precision = prec - 1;
    sci_spec.flags &= ~(FMT_FLAG_SIGN | FMT_FLAG_SPACE);
    sci_spec.value.double_value = val;
    formatted_len = format_scientific(&temp_buffer, &sci_spec);

    if (!(spec->flags & FMT_FLAG_ALT)) {
      size_t e_pos = 0;
      for (size_t i = 0; i < formatted_len; i++) {
        if (temp_buf[i] == 'e' || temp_buf[i] == 'E') {
          e_pos = i;
          break;
        }
      }

      if (e_pos > 0) {
        size_t mantissa_len = trim_trailing_zeros(temp_buf, e_pos, false);
        memmove(temp_buf + mantissa_len, temp_buf + e_pos, formatted_len - e_pos);
        formatted_len = mantissa_len + (formatted_len - e_pos);
      }
    }
  } else {
    fmt_spec_t fixed_spec = *spec;
    int integer_digits = (exp >= 0) ? exp + 1 : 0;
    int decimal_places = prec - integer_digits;
    if (decimal_places < 0) decimal_places = 0;

    fixed_spec.precision = decimal_places;
    fixed_spec.flags &= ~(FMT_FLAG_SIGN | FMT_FLAG_SPACE);
    fixed_spec.value.double_value = val;
    formatted_len = format_double(&temp_buffer, &fixed_spec);

    if (!(spec->flags & FMT_FLAG_ALT)) {
      formatted_len = trim_trailing_zeros(temp_buf, formatted_len, false);
    }
  }

  n += fmtlib_buffer_write(buffer, temp_buf, formatted_len);
  return n;
}

static size_t format_hex_float(fmt_buffer_t *buffer, const fmt_spec_t *spec) {
  union double_raw v = { .value = spec->value.double_value };
  size_t n = 0;
  bool uppercase = (spec->flags & FMT_FLAG_UPPER) != 0;
  const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";

  if (v.sign) {
    n += fmtlib_buffer_write_char(buffer, '-');
  } else if (spec->flags & FMT_FLAG_SIGN) {
    n += fmtlib_buffer_write_char(buffer, '+');
  } else if (spec->flags & FMT_FLAG_SPACE) {
    n += fmtlib_buffer_write_char(buffer, ' ');
  }

  if (v.exp == 0x7FF && v.frac == 0) {
    const char *inf = uppercase ? "INF" : "inf";
    n += fmtlib_buffer_write(buffer, inf, 3);
    return n;
  } else if (v.exp == 0x7FF && v.frac != 0) {
    const char *nan_str = uppercase ? "NAN" : "nan";
    n += fmtlib_buffer_write(buffer, nan_str, 3);
    return n;
  }

  int exponent;
  char lead_digit;
  uint64_t frac_bits = v.frac;

  if (v.exp == 0 && v.frac == 0) {
    lead_digit = '0';
    exponent = 0;
  } else if (v.exp == 0) {
    lead_digit = '0';
    exponent = -1022;
  } else {
    lead_digit = '1';
    exponent = (int)v.exp - 1023;
  }

  n += fmtlib_buffer_write(buffer, uppercase ? "0X" : "0x", 2);
  n += fmtlib_buffer_write_char(buffer, lead_digit);

  int max_hex_digits = 13;
  int prec;
  bool has_precision = spec->precision >= 0;

  if (has_precision) {
    prec = spec->precision;
    if (prec < max_hex_digits) {
      int shift = (max_hex_digits - prec) * 4;
      uint64_t half = 1ULL << (shift - 1);
      uint64_t mask = ~((1ULL << shift) - 1);
      uint64_t remainder = frac_bits & ~mask;
      if (remainder > half || (remainder == half && ((frac_bits >> shift) & 1))) {
        frac_bits = (frac_bits & mask) + (1ULL << shift);
        if (frac_bits >= (1ULL << 52)) {
          frac_bits = 0;
          if (lead_digit == '1') {
            exponent++;
          } else {
            lead_digit = '1';
          }
        }
      } else {
        frac_bits &= mask;
      }
    }
  } else {
    prec = max_hex_digits;
    uint64_t tmp = frac_bits;
    while (prec > 0 && (tmp & 0xF) == 0) {
      prec--;
      tmp >>= 4;
    }
  }

  if (prec > 0 || (spec->flags & FMT_FLAG_ALT)) {
    n += fmtlib_buffer_write_char(buffer, '.');

    for (int i = 0; i < prec && i < max_hex_digits; i++) {
      int shift = 48 - (i * 4);
      int nibble = (int)((frac_bits >> shift) & 0xF);
      n += fmtlib_buffer_write_char(buffer, digits[nibble]);
    }
    for (int i = max_hex_digits; i < prec; i++) {
      n += fmtlib_buffer_write_char(buffer, '0');
    }
  }

  n += fmtlib_buffer_write_char(buffer, uppercase ? 'P' : 'p');
  n += fmtlib_buffer_write_char(buffer, exponent >= 0 ? '+' : '-');
  if (exponent < 0) exponent = -exponent;

  char temp[16];
  size_t len = u64_to_str((uint64_t)exponent, temp, &decimal_format);
  n += fmtlib_buffer_write(buffer, temp, len);

  return n;
}

// Writes a signed or unsigned number to the buffer using the given format.
static inline size_t format_integer(fmt_buffer_t *buffer, const fmt_spec_t *spec, bool is_signed, const struct num_format *format) {
  int width = min(max(spec->width, 0), FMTLIB_MAX_WIDTH);
  size_t n = 0;
  uint64_t v;
  bool is_negative = false;
  if (is_signed) {
    int64_t i = (int64_t) spec->value.uint64_value;
    if (i < 0) {
      v = -(uint64_t)i; // negate in unsigned to avoid UB on INT64_MIN
      is_negative = true;
    } else {
      v = i;
    }
  } else {
    v = spec->value.uint64_value;
  }

  // write sign or space to buffer
  if (is_negative) {
    n += fmtlib_buffer_write_char(buffer, '-');
  } else if (spec->flags & FMT_FLAG_SIGN) {
    n += fmtlib_buffer_write_char(buffer, '+');
  } else if (spec->flags & FMT_FLAG_SPACE) {
    n += fmtlib_buffer_write_char(buffer, ' ');
  }

  // write prefix for alternate form (e.g. 0x) to buffer
  if (spec->flags & FMT_FLAG_ALT) {
    const char *ptr = format->prefix;
    while (*ptr) {
      n += fmtlib_buffer_write_char(buffer, *ptr++);
    }
  }

  // write digits to an intermediate buffer so we can calculate the
  // length of the number and apply precision and padding accordingly
  char temp[TEMP_BUFFER_SIZE];
  size_t len = u64_to_str(v, temp, format);

  // posix: zero value with explicit precision of 0 produces no digits
  if (v == 0 && spec->precision == 0) {
    len = 0;
  }

  // pad with leading zeros to reach specified precision
  if (spec->precision > 0 && (size_t)spec->precision > len) {
    size_t padding = spec->precision - len;
    for (size_t i = 0; i < padding; i++) {
      n += fmtlib_buffer_write_char(buffer, '0');
    }
  }

  // left-pad number with zeros to reach specified width
  // posix: for integer conversions, 0 flag is ignored when precision is specified
  if (spec->flags & FMT_FLAG_ZERO && spec->precision <= 0 && (size_t)width > len + n) {
    // normally padding is handled outside of this function and is applied to the
    // entire number including the sign or prefix. however, when the zero flag is
    // set, the zero padding is applied to the number only and keeps the sign or
    // prefix in front of the number.
    if ((size_t)width > len + n) {
      size_t padding = width - len - n;
      for (size_t i = 0; i < padding; i++) {
        n += fmtlib_buffer_write_char(buffer, '0');
      }
    }
  }

  // finally write the number to the buffer
  n += fmtlib_buffer_write(buffer, temp, len);
  return n;
}

static size_t format_signed(fmt_buffer_t *buffer, const fmt_spec_t *spec) {
  return format_integer(buffer, spec, true, &decimal_format);
}

static size_t format_unsigned(fmt_buffer_t *buffer, const fmt_spec_t *spec) {
  return format_integer(buffer, spec, false, &decimal_format);
}

static size_t format_binary(fmt_buffer_t *buffer, const fmt_spec_t *spec) {
  return format_integer(buffer, spec, false, &binary_format);
}

static size_t format_octal(fmt_buffer_t *buffer, const fmt_spec_t *spec) {
  if ((spec->flags & FMT_FLAG_PRINTF) && (spec->flags & FMT_FLAG_ALT)) {
    // posix: %#o increases precision to force first digit to be 0
    fmt_spec_t modified = *spec;
    modified.flags &= ~FMT_FLAG_ALT;
    uint64_t v = spec->value.uint64_value;
    int digits = 0;
    uint64_t tmp = v;
    do { digits++; tmp /= 8; } while (tmp > 0);
    if (modified.precision < digits + 1) {
      modified.precision = digits + 1;
    }
    if (v == 0 && spec->precision <= 0) {
      modified.precision = -1;
    }
    return format_integer(buffer, &modified, false, &octal_format);
  }
  return format_integer(buffer, spec, false, &octal_format);
}

static size_t format_hex(fmt_buffer_t *buffer, const fmt_spec_t *spec) {
  if (spec->flags & FMT_FLAG_UPPER) {
    return format_integer(buffer, spec, false, &hex_upper_format);
  } else {
    return format_integer(buffer, spec, false, &hex_lower_format);
  }
}

static inline size_t fmtlib_buffer_write_u64(fmt_buffer_t *buffer, uint64_t value) {
  char temp[TEMP_BUFFER_SIZE];
  size_t len = u64_to_str(value, temp, &decimal_format);
  return fmtlib_buffer_write(buffer, temp, len);
}

// Writes a floating point number to the buffer.
static inline size_t format_double(fmt_buffer_t *buffer, const fmt_spec_t *spec) {
  union double_raw v = { .value = spec->value.double_value };
  size_t width = (size_t) min(max(spec->width, 0), FMTLIB_MAX_WIDTH);
  size_t prec = (size_t) min((spec->precision < 0 ? PRECISION_DEFAULT : spec->precision), PRECISION_MAX);
  size_t n = 0;

  // write sign or space to buffer
  if (v.sign) {
    n += fmtlib_buffer_write_char(buffer, '-');
  } else if (spec->flags & FMT_FLAG_SIGN) {
    n += fmtlib_buffer_write_char(buffer, '+');
  } else if (spec->flags & FMT_FLAG_SPACE) {
    n += fmtlib_buffer_write_char(buffer, ' ');
  }

  // handle special encodings
  if (v.exp == 0x7FF && v.frac == 0) {
    const char *inf = spec->flags & FMT_FLAG_UPPER ? "INF" : "inf";
    n += fmtlib_buffer_write(buffer, inf, 3);
    return n;
  } else if (v.exp == 0x7FF && v.frac != 0) {
    const char *nan = spec->flags & FMT_FLAG_UPPER ? "NAN" : "nan";
    n += fmtlib_buffer_write(buffer, nan, 3);
    return n;
  } else if (v.exp == 0 && v.frac == 0) {
    n += fmtlib_buffer_write_char(buffer, '0');
    if (prec > 0 || (spec->flags & FMT_FLAG_ALT)) {
      n += fmtlib_buffer_write_char(buffer, '.');
      for (size_t i = 0; i < prec; i++) {
        n += fmtlib_buffer_write_char(buffer, '0');
      }
    }
    return n;
  }

  if (v.value < 0) {
    v.value = -v.value;
  }

  uint64_t whole = (uint64_t) v.value;
  uint64_t frac;

  double tmp = (v.value - (double)whole) * pow10[prec];
  frac = (uint64_t) tmp;

  double delta = tmp - (double)frac;
  if (delta > 0.5) {
    frac++;
    if (frac >= (uint64_t)pow10[prec]) {
      frac = 0;
      whole++;
    }
  } else if (delta < 0.5) {
    // do nothing
  } else if ((frac == 0) || (frac & 1)) {
    frac++;
  }

  bool write_decimal = true;
  if (prec == 0) {
    write_decimal = (spec->flags & FMT_FLAG_ALT);
  }

  char temp[TEMP_BUFFER_SIZE];
  size_t len = u64_to_str(whole, temp, &decimal_format);
  size_t frac_len = 0;
  if (write_decimal) {
    temp[len++] = '.';
    if (prec > 0) {
      frac_len = u64_to_str(frac, temp + len, &decimal_format);
      if (frac_len < prec) {
        memmove(temp + len + (prec - frac_len), temp + len, frac_len);
        for (size_t i = 0; i < prec - frac_len; i++) {
          temp[len + i] = '0';
        }
        frac_len = prec;
      }
      len += frac_len;
    }
  }

  // left-pad number with zeros to reach specified width
  if (spec->flags & FMT_FLAG_ZERO && width > len + n) {
    if (width > len + n) {
      size_t padding = width - len - n;
      for (size_t i = 0; i < padding; i++) {
        n += fmtlib_buffer_write_char(buffer, '0');
      }
    }
  }

  n += fmtlib_buffer_write(buffer, temp, len);
  return n;
}

static size_t format_string(fmt_buffer_t *buffer, const fmt_spec_t *spec) {
  const char *str = spec->value.voidptr_value;
  if (str == NULL) {
    str = "(null)";
    size_t len = 6;
    if (spec->precision >= 0 && (size_t)spec->precision < len) {
      len = spec->precision;
    }
    return fmtlib_buffer_write(buffer, str, len);
  }
  size_t len;
  if (spec->precision >= 0) {
    len = 0;
    while (len < (size_t)spec->precision && str[len] != '\0') {
      len++;
    }
  } else {
    len = strlen(str);
  }

  return fmtlib_buffer_write(buffer, str, len);
}

static size_t format_char(fmt_buffer_t *buffer, const fmt_spec_t *spec) {
  char c = *((char *)&spec->value);
  char tmpbuf[8];
  unsigned tmplen = 0;
  if (c == 0) {
    tmpbuf[tmplen++] = '\\';
    tmpbuf[tmplen++] = '0';
  } else if (spec->flags & FMT_FLAG_ALT) {
    // write control characters using the ^X notation
    if (c < 0x20 || c == 0x7F) {
      tmpbuf[tmplen++] = '^';
      if ((unsigned char)c == 0x7F) {
        tmpbuf[tmplen++] = '?';
      } else {
        tmpbuf[tmplen++] = (char)(c + '@');
      }
    } else {
      tmpbuf[tmplen++] = c;
    }
  } else {
    tmpbuf[tmplen++] = c;
  }

  return fmtlib_buffer_write(buffer, tmpbuf, tmplen);
}

// ===========================================================================
// OS custom formatters
// ===========================================================================

static size_t format_mem_quantity(fmt_buffer_t *buffer, const fmt_spec_t *spec) {
  static const char *suffix_lower[] = {"", "k", "m", "g", "t", "p", "e", "z", "y"};
  static const char *suffix_upper[] = {"", "K", "M", "G", "T", "P", "E", "Z", "Y"};
  static const char *suffix_lower_alt[] = {"b", "ki", "mi", "gi", "ti", "pi", "ei", "zi", "yi"};
  static const char *suffix_upper_alt[] = {"B", "Ki", "Mi", "Gi", "Ti", "Pi", "Ei", "Zi", "Yi"};
  static size_t num_suffixes = sizeof(suffix_lower) / sizeof(suffix_lower[0]);

  uint64_t value = spec->value.uint64_value;
  const char **suffixes;
  if (spec->flags & FMT_FLAG_ALT) {
    suffixes = (spec->flags & FMT_FLAG_UPPER) ? suffix_upper_alt : suffix_lower_alt;
  } else {
    suffixes = (spec->flags & FMT_FLAG_UPPER) ? suffix_upper : suffix_lower;
  }

  size_t suffix_index = 0;
  while (value >= 1024 && suffix_index < num_suffixes - 1) {
    value /= 1024;
    suffix_index++;
  }

  char temp[TEMP_BUFFER_SIZE];
  size_t len = u64_to_str(value, temp, &decimal_format);
  size_t n = fmtlib_buffer_write(buffer, temp, len);
  n += fmtlib_buffer_write(buffer, suffixes[suffix_index], strlen(suffixes[suffix_index]));
  return n;
}

static size_t format_errno(fmt_buffer_t *buffer, const fmt_spec_t *spec) {
  int err = (int) spec->value.uint64_value;
  size_t n = 0;
  if (err >= 0) {
    n += fmtlib_buffer_write(buffer, "success<", 8);
    n += format_unsigned(buffer, &mk_fmtlib_spec_u64(err, 0));
    n += fmtlib_buffer_write_char(buffer, '>');
    return n;
  }

  const char *str = strerror(abs(err));
  if (str == NULL) {
    n = fmtlib_buffer_write(buffer, "{unknown error: ", 16);
    n += format_signed(buffer, spec);
    n += fmtlib_buffer_write_char(buffer, '}');
    return n;
  }
  return fmtlib_buffer_write(buffer, str, strlen(str));
}

static size_t format_path_t(fmt_buffer_t *buffer, const fmt_spec_t *spec) {
  const path_t *path = spec->value.voidptr_value;
  if (path == NULL) {
    return fmtlib_buffer_write(buffer, "(null)", 6);
  }
  return fmtlib_buffer_write(buffer, path_start(*path), path_len(*path));
}

static size_t format_str_t(fmt_buffer_t *buffer, const fmt_spec_t *spec) {
  const str_t *str = spec->value.voidptr_value;
  if (str == NULL) {
    return fmtlib_buffer_write(buffer, "(null str_t)", 12);
  } else if (str_isnull(*str)) {
    return fmtlib_buffer_write(buffer, "(null)", 6);
  }
  return fmtlib_buffer_write(buffer, str_cptr(*str), str_len(*str));
}

static size_t format_cstr_t(fmt_buffer_t *buffer, const fmt_spec_t *spec) {
  const cstr_t *str = spec->value.voidptr_value;
  if (str == NULL) {
    return fmtlib_buffer_write(buffer, "(null)", 6);
  }
  return fmtlib_buffer_write(buffer, cstr_ptr(*str), cstr_len(*str));
}

static size_t format_struct_tm_utc(fmt_buffer_t *buffer, struct tm *tm) {
  size_t n = 0;
  n += format_unsigned(buffer, &mk_fmtlib_spec_u64(tm->tm_year + 1900, 4));
  n += fmtlib_buffer_write_char(buffer, '-');
  n += format_unsigned(buffer, &mk_fmtlib_spec_u64(tm->tm_mon + 1, 2));
  n += fmtlib_buffer_write_char(buffer, '-');
  n += format_unsigned(buffer, &mk_fmtlib_spec_u64(tm->tm_mday, 2));
  n += fmtlib_buffer_write_char(buffer, ' ');
  n += format_unsigned(buffer, &mk_fmtlib_spec_u64(tm->tm_hour, 2));
  n += fmtlib_buffer_write_char(buffer, ':');
  n += format_unsigned(buffer, &mk_fmtlib_spec_u64(tm->tm_min, 2));
  n += fmtlib_buffer_write_char(buffer, ':');
  n += format_unsigned(buffer, &mk_fmtlib_spec_u64(tm->tm_sec, 2));
  return n;
}

static size_t format_time_utc(fmt_buffer_t *buffer, const fmt_spec_t *spec) {
  struct tm *tm = spec->value.voidptr_value;
  if (tm == NULL) {
    return fmtlib_buffer_write(buffer, "(null)", 6);
  }
  return format_struct_tm_utc(buffer, tm);
}

static size_t format_time_unix(fmt_buffer_t *buffer, const fmt_spec_t *spec) {
  uint64_t epoch = spec->value.uint64_value;
  if (epoch == 0) {
    return fmtlib_buffer_write(buffer, "1970-01-01 00:00:00", 19);
  }

  struct tm tm = {0};
  posix2tm(epoch, &tm);
  return format_struct_tm_utc(buffer, &tm);
}

static size_t format_process(fmt_buffer_t *buffer, const fmt_spec_t *spec) {
  const struct proc *proc = spec->value.voidptr_value;
  if (proc == NULL) {
    return fmtlib_buffer_write(buffer, "(null)", 6);
  }

  size_t n = 0;
  n += fmtlib_buffer_write_char(buffer, '(');
  n += fmtlib_buffer_write_u64(buffer, proc->pid);
  n += fmtlib_buffer_write_char(buffer, ':');
  if (str_len(proc->name) > 0) {
    n += fmtlib_buffer_write_char(buffer, ':');
    n += fmtlib_buffer_write(buffer, str_cptr(proc->name), str_len(proc->name));
  }
  n += fmtlib_buffer_write_char(buffer, ')');
  return n;
}

static size_t format_thread(fmt_buffer_t *buffer, const fmt_spec_t *spec) {
  const struct thread *td = spec->value.voidptr_value;
  if (td == NULL) {
    return fmtlib_buffer_write(buffer, "(null)", 6);
  }

  size_t n = 0;
  n += fmtlib_buffer_write_char(buffer, '(');
  n += fmtlib_buffer_write_u64(buffer, td->proc->pid);
  n += fmtlib_buffer_write_char(buffer, ':');
  n += fmtlib_buffer_write_u64(buffer, td->tid);
  if (str_len(td->name) > 0) {
    n += fmtlib_buffer_write_char(buffer, ':');
    n += fmtlib_buffer_write(buffer, str_cptr(td->name), str_len(td->name));
  }
  n += fmtlib_buffer_write_char(buffer, ')');
  return n;
}

static size_t format_lock_object(fmt_buffer_t *buffer, const fmt_spec_t *spec) {
  const struct lock_object *lo = spec->value.voidptr_value;
  bool alt = (spec->flags & FMT_FLAG_ALT) != 0;
  bool upper = (spec->flags & FMT_FLAG_UPPER) != 0;
  if (lo == NULL) {
    return fmtlib_buffer_write(buffer, "(null)", 6);
  }

  size_t n = 0;
  n += fmtlib_buffer_write(buffer, "{", 1);

  size_t lo_name_len = strlen(lo->name);
  bool write_pointer = alt || (lo_name_len == 0);
  n += fmtlib_buffer_write(buffer, lo->name, strlen(lo->name));
  if (write_pointer) {
    n += fmtlib_buffer_write_char(buffer, '<');

    fmt_spec_t ptr_spec = mk_fmtlib_spec_voidp(lo);
    ptr_spec.flags = FMT_FLAG_ALT | FMT_FLAG_ZERO | (upper ? FMT_FLAG_UPPER : 0);
    ptr_spec.width = 16;
    n += format_hex(buffer, &ptr_spec);
    n += fmtlib_buffer_write_char(buffer, '>');
  }

  n += fmtlib_buffer_write_char(buffer, ':');

  switch (LO_LOCK_CLASS(lo)) {
    case SPINLOCK_LOCKCLASS:
      if (upper)
        n += fmtlib_buffer_write(buffer, "spinlock", 8);
      else
        n += fmtlib_buffer_write(buffer, "spl", 3);
      break;
    case MUTEX_LOCKCLASS:
      if (upper)
        n += fmtlib_buffer_write(buffer, "mutex", 5);
      else
        n += fmtlib_buffer_write(buffer, "mtx", 3);
      break;
    case RWLOCK_LOCKCLASS:
      if (upper)
        n += fmtlib_buffer_write(buffer, "rwlock", 6);
      else
        n += fmtlib_buffer_write(buffer, "rwl", 3);
      break;
    default:
      n += fmtlib_buffer_write(buffer, "invalid", 7);
      n += fmtlib_buffer_write_char(buffer, '<');
      n += fmtlib_buffer_write_u64(buffer, LO_LOCK_CLASS(lo));
      n += fmtlib_buffer_write_char(buffer, '>');
      n += fmtlib_buffer_write_char(buffer, '}');
      return n;
  }

  uint32_t lo_opts = LO_LOCK_OPTS(lo);
  if (upper) {
    if (lo_opts & LO_DEBUG) n += fmtlib_buffer_write(buffer, ",debug", 6);
    if (lo_opts & LO_NOCLAIMS) n += fmtlib_buffer_write(buffer, ",noclaims", 9);
    if (lo_opts & LO_RECURSABLE) n += fmtlib_buffer_write(buffer, ",recurse", 8);
    if (lo_opts & LO_SLEEPABLE) n += fmtlib_buffer_write(buffer, ",sleep", 6);
    if (lo_opts & LO_INITIALIZED) n += fmtlib_buffer_write(buffer, ",init", 5);
  } else {
    n += fmtlib_buffer_write_char(buffer, ':');
    if (lo_opts & LO_DEBUG) n += fmtlib_buffer_write_char(buffer, 'D');
    if (lo_opts & LO_NOCLAIMS) n += fmtlib_buffer_write_char(buffer, 'N');
    if (lo_opts & LO_RECURSABLE) n += fmtlib_buffer_write_char(buffer, 'R');
    if (lo_opts & LO_SLEEPABLE) n += fmtlib_buffer_write_char(buffer, 'S');
    if (lo_opts & LO_INITIALIZED) n += fmtlib_buffer_write_char(buffer, 'i');
  }

  n += fmtlib_buffer_write_char(buffer, ':');
  n += fmtlib_buffer_write_u64(buffer, lo->data);
  n += fmtlib_buffer_write_char(buffer, '}');
  return n;
}

static size_t format_vtype(fmt_buffer_t *buffer, const fmt_spec_t *spec) {
  enum vtype vtype = spec->value.uint64_value;
  bool upper = (spec->flags & FMT_FLAG_UPPER) != 0;

  size_t n = 0;
  switch (vtype) {
    case V_NONE:
      n += fmtlib_buffer_write(buffer, (upper ? "V_NONE" : "v_none"), 6);
      return n;
    case V_REG:
      n += fmtlib_buffer_write(buffer, (upper ? "V_REG" : "v_reg"), 5);
      return n;
    case V_DIR:
      n += fmtlib_buffer_write(buffer, (upper ? "V_DIR" : "v_dir"), 5);
      return n;
    case V_LNK:
      n += fmtlib_buffer_write(buffer, (upper ? "V_LNK" : "v_lnk"), 5);
      return n;
    case V_BLK:
      n += fmtlib_buffer_write(buffer, (upper ? "V_BLK" : "v_blk"), 5);
      return n;
    case V_CHR:
      n += fmtlib_buffer_write(buffer, (upper ? "V_CHR" : "v_chr"), 5);
      return n;
    case V_FIFO:
      n += fmtlib_buffer_write(buffer, (upper ? "V_FIFO" : "v_fifo"), 6);
      return n;
    case V_SOCK:
      n += fmtlib_buffer_write(buffer, (upper ? "V_SOCK" : "v_sock"), 6);
      return n;
    default:
      n += fmtlib_buffer_write(buffer, upper ? "INVALID VTYPE<" : "invalid vtype<", 14);
      n += fmtlib_buffer_write_u64(buffer, vtype);
      n += fmtlib_buffer_write_char(buffer, '>');
      return n;
  }
}

static size_t format_ftype(fmt_buffer_t *buffer, const fmt_spec_t *spec) {
  enum ftype ftype = spec->value.uint64_value;
  bool upper = (spec->flags & FMT_FLAG_UPPER) != 0;

  size_t n = 0;
  switch (ftype) {
    case FT_VNODE:
      n += fmtlib_buffer_write(buffer, (upper ? "VNODE" : "vnode"), 5);
      return n;
    case FT_PIPE:
      n += fmtlib_buffer_write(buffer, (upper ? "PIPE" : "pipe"), 4);
      return n;
    case FT_PTS:
      n += fmtlib_buffer_write(buffer, (upper ? "PTS" : "pts"), 3);
      return n;
    case FT_SOCK:
      n += fmtlib_buffer_write(buffer, (upper ? "SOCK" : "sock"), 4);
      return n;
    case FT_EPOLL:
      n += fmtlib_buffer_write(buffer, (upper ? "EPOLL" : "epoll"), 5);
      return n;
    case FT_EVENTFD:
      n += fmtlib_buffer_write(buffer, (upper ? "EVENTFD" : "eventfd"), 7);
      return n;
    default:
      n += fmtlib_buffer_write(buffer, upper ? "INVALID FTYPE<" : "invalid ftype<", 14);
      n += fmtlib_buffer_write_u64(buffer, ftype);
      n += fmtlib_buffer_write_char(buffer, '>');
      return n;
  }
}

static size_t format_vattr(fmt_buffer_t *buffer, const fmt_spec_t *spec) {
  const struct vattr *vattr = spec->value.voidptr_value;
  if (vattr == NULL) {
    return fmtlib_buffer_write(buffer, "(null)", 6);
  }

  size_t n = 0;
  n += fmtlib_buffer_write_char(buffer, '{');
  n += format_vtype(buffer, &mk_fmtlib_spec_u64(vattr->type, 0));
  n += fmtlib_buffer_write_char(buffer, ',');
  n += format_octal(buffer, &mk_fmtlib_spec_u64(vattr->mode, 0));
  n += fmtlib_buffer_write_char(buffer, '}');
  return n;
}

static size_t format_vnode(fmt_buffer_t *buffer, const fmt_spec_t *spec) {
  const vnode_t *vn = spec->value.voidptr_value;
  bool plus = (spec->flags & FMT_FLAG_SIGN) != 0;
  if (vn == NULL) {
    return fmtlib_buffer_write(buffer, "(null)", 6);
  }

  size_t n = 0;
  fmt_spec_t tmp_spec;
  n += fmtlib_buffer_write_char(buffer, '(');
  if (vn->vfs != NULL) {
    tmp_spec = mk_fmtlib_spec_u64(vn->vfs->id, 0);
    n += format_signed(buffer, &tmp_spec);
  } else {
    n += fmtlib_buffer_write(buffer, "null", 4);
  }
  n += fmtlib_buffer_write_char(buffer, ':');
  tmp_spec = mk_fmtlib_spec_u64(vn->id, 0);
  n += format_signed(buffer, &tmp_spec);
  n += fmtlib_buffer_write_char(buffer, ')');

  if (plus) {
    n += fmtlib_buffer_write_char(buffer, '<');
    tmp_spec = mk_fmtlib_spec_pointer(vn);
    n += format_hex(buffer, &tmp_spec);
    n += fmtlib_buffer_write_char(buffer, '>');
  }
  return n;
}

static size_t format_ventry(fmt_buffer_t *buffer, const fmt_spec_t *spec) {
  const ventry_t *ve = spec->value.voidptr_value;
  bool plus = (spec->flags & FMT_FLAG_SIGN) != 0;
  if (ve == NULL) {
    return fmtlib_buffer_write(buffer, "(null)", 6);
  }

  size_t n = 0;
  fmt_spec_t tmp_spec;
  n += fmtlib_buffer_write_char(buffer, '(');
  if (ve->vn != NULL && ve->vn->vfs == NULL) {
    n += fmtlib_buffer_write(buffer, "null", 4);
  } else {
    tmp_spec = mk_fmtlib_spec_u64(ve->vfs_id, 0);
    n += format_signed(buffer, &tmp_spec);
  }
  n += fmtlib_buffer_write_char(buffer, ':');
  if (!((ve)->flags & VE_LINKED)) {
    n += fmtlib_buffer_write(buffer, "null", 4);
  } else {
    tmp_spec = mk_fmtlib_spec_u64(ve->id, 0);
    n += format_signed(buffer, &tmp_spec);
  }
  n += fmtlib_buffer_write_char(buffer, ':');
  tmp_spec = mk_fmtlib_spec_voidp(&ve->name);
  n += format_str_t(buffer, &tmp_spec);
  n += fmtlib_buffer_write_char(buffer, ')');

  if (plus) {
    n += fmtlib_buffer_write_char(buffer, '<');
    tmp_spec = mk_fmtlib_spec_pointer(ve);
    n += format_hex(buffer, &tmp_spec);
    n += fmtlib_buffer_write_char(buffer, '>');
  }
  return n;
}

static size_t format_file(fmt_buffer_t *buffer, const fmt_spec_t *spec) {
  const file_t *f = spec->value.voidptr_value;
  bool plus = (spec->flags & FMT_FLAG_SIGN) != 0;
  if (f == NULL) {
    return fmtlib_buffer_write(buffer, "(null)", 6);
  }

  size_t n = 0;
  n += fmtlib_buffer_write_char(buffer, '<');
  n += format_ftype(buffer, &mk_fmtlib_spec_u64(f->type, 0));
  n += fmtlib_buffer_write_char(buffer, ':');
  n += format_signed(buffer, &mk_fmtlib_spec_pointer(f->data));
  n += fmtlib_buffer_write_char(buffer, '>');

  if (plus) {
    n += fmtlib_buffer_write_char(buffer, '<');
    n += format_hex(buffer, &mk_fmtlib_spec_pointer(f));
    n += fmtlib_buffer_write_char(buffer, '>');
  }
  return n;
}

static size_t format_ipv4(fmt_buffer_t *buffer, const fmt_spec_t *spec) {
  uint32_t ip = spec->value.uint64_value;
  uint8_t octets[4] = {
    (ip >> 24) & 0xFF,
    (ip >> 16) & 0xFF,
    (ip >> 8) & 0xFF,
    (ip >> 0) & 0xFF
  };

  char temp[TEMP_BUFFER_SIZE];
  size_t n = 0;
  for (int i = 0; i < 4; i++) {
    size_t len = u64_to_str(octets[i], temp, &decimal_format);
    n += fmtlib_buffer_write(buffer, temp, len);
    if (i < 3) {
      n += fmtlib_buffer_write_char(buffer, '.');
    }
  }
  return n;
}

static size_t format_mac(fmt_buffer_t *buffer, const fmt_spec_t *spec) {
  const uint8_t *mac = spec->value.voidptr_value;
  if (mac == NULL) {
    return fmtlib_buffer_write(buffer, "(null)", 6);
  }

  size_t n = 0;
  for (int i = 0; i < 6; i++) {
    fmt_spec_t byte_spec = mk_fmtlib_spec_u64(mac[i], 2);
    byte_spec.flags = FMT_FLAG_ZERO;
    byte_spec.fill_char = '0';
    n += format_hex(buffer, &byte_spec);
    if (i < 5) {
      n += fmtlib_buffer_write_char(buffer, ':');
    }
  }
  return n;
}

// ===========================================================================
// Alignment and type resolution
// ===========================================================================

// aligns the string to the spec width
static inline size_t apply_alignment(fmt_buffer_t *buffer, const fmt_spec_t *spec, const char *str, size_t len) {
  if (len > (size_t)spec->width) {
    return fmtlib_buffer_write(buffer, str, len);
  }

  size_t n = 0;
  size_t padding = spec->width - len;
  char pad_char = spec->fill_char;
  switch (spec->align) {
    case FMT_ALIGN_LEFT:
      n += fmtlib_buffer_write(buffer, str, len);
      for (size_t i = 0; i < padding; i++) {
        n += fmtlib_buffer_write_char(buffer, pad_char);
      }
      break;
    case FMT_ALIGN_RIGHT:
      for (size_t i = 0; i < padding; i++) {
        n += fmtlib_buffer_write_char(buffer, pad_char);
      }
      n += fmtlib_buffer_write(buffer, str, len);
      break;
    case FMT_ALIGN_CENTER:
      // for odd padding, put the extra character on the left
      for (size_t i = 0; i < (padding + 1) / 2; i++) {
        n += fmtlib_buffer_write_char(buffer, pad_char);
      }
      n += fmtlib_buffer_write(buffer, str, len);
      for (size_t i = 0; i < padding / 2; i++) {
        n += fmtlib_buffer_write_char(buffer, pad_char);
      }
      break;
  }
  return n;
}

static inline size_t resolve_integral_type(fmt_spec_t *spec) {
  size_t n = 0;
  fmt_argtype_t argtype;
  fmt_formatter_t formatter = NULL;
  int flags = spec->flags;
  const char *ptr = spec->type;

  if (ptr[n] == 'l') {
    n += 1;
    if (ptr[n] == 'l') {
      n += 1;
    }
    argtype = FMT_ARGTYPE_INT64;
  } else if (ptr[n] == 'z') {
    argtype = FMT_ARGTYPE_SIZE;
    n += 1;
  } else {
    argtype = FMT_ARGTYPE_INT32;
  }

  switch (ptr[n]) {
    case 'd': case 'i': formatter = format_signed; break;
    case 'u': formatter = format_unsigned; break;
    case 'b': formatter = format_binary; break;
    case 'o': formatter = format_octal; break;
    case 'X': flags |= FMT_FLAG_UPPER; // fallthrough
    case 'x': formatter = format_hex; break;
    default:
      return 0; // unknown type
  }

  spec->flags = flags;
  spec->argtype = argtype;
  spec->formatter = formatter;
  return 1;
}

// MARK: Public API

int fmtlib_resolve_type(fmt_spec_t *spec) {
  if (spec->type_len == 0) {
    spec->argtype = FMT_ARGTYPE_NONE;
    spec->formatter = NULL;
    return 1;
  }

  // OS custom types (checked first so they take priority over standard types)
  if (spec->type_len >= 2) {
    if (strncmp("pr", spec->type, 2) == 0) {
      spec->argtype = FMT_ARGTYPE_VOIDPTR;
      spec->formatter = format_process;
      return 1;
    }
    if (strncmp("td", spec->type, 2) == 0) {
      spec->argtype = FMT_ARGTYPE_VOIDPTR;
      spec->formatter = format_thread;
      return 1;
    }
    if (strncmp("Lo", spec->type, 2) == 0) {
      spec->argtype = FMT_ARGTYPE_VOIDPTR;
      spec->formatter = format_lock_object;
      return 1;
    }
    if (strncmp("va", spec->type, 2) == 0) {
      spec->argtype = FMT_ARGTYPE_VOIDPTR;
      spec->formatter = format_vattr;
      return 1;
    }
    if (strncmp("ve", spec->type, 2) == 0) {
      spec->argtype = FMT_ARGTYPE_VOIDPTR;
      spec->formatter = format_ventry;
      return 1;
    }
    if (strncmp("vn", spec->type, 2) == 0) {
      spec->argtype = FMT_ARGTYPE_VOIDPTR;
      spec->formatter = format_vnode;
      return 1;
    }
    if (strncmp("vt", spec->type, 2) == 0) {
      spec->argtype = FMT_ARGTYPE_INT32;
      spec->formatter = format_vtype;
      return 1;
    }
    if (strncmp("ip", spec->type, 2) == 0) {
      spec->argtype = FMT_ARGTYPE_INT32;
      spec->formatter = format_ipv4;
      return 1;
    }
    if (strncmp("err", spec->type, 3) == 0) {
      spec->argtype = FMT_ARGTYPE_INT32;
      spec->formatter = format_errno;
      return 1;
    }
    if (strncmp("str", spec->type, 3) == 0) {
      spec->argtype = FMT_ARGTYPE_VOIDPTR;
      spec->formatter = format_str_t;
      return 1;
    }
    if (strncmp("mac", spec->type, 3) == 0) {
      spec->argtype = FMT_ARGTYPE_VOIDPTR;
      spec->formatter = format_mac;
      return 1;
    }
    if (strncmp("cstr", spec->type, 4) == 0) {
      spec->argtype = FMT_ARGTYPE_VOIDPTR;
      spec->formatter = format_cstr_t;
      return 1;
    }
    if (strncmp("path", spec->type, 4) == 0) {
      spec->argtype = FMT_ARGTYPE_VOIDPTR;
      spec->formatter = format_path_t;
      return 1;
    }
    if (strncmp("time", spec->type, 4) == 0) {
      spec->argtype = FMT_ARGTYPE_VOIDPTR;
      spec->formatter = format_time_utc;
      return 1;
    }
    if (strncmp("epoc", spec->type, 4) == 0) {
      spec->argtype = FMT_ARGTYPE_INT64;
      spec->formatter = format_time_unix;
      return 1;
    }
    if (strncmp("file", spec->type, 4) == 0) {
      spec->argtype = FMT_ARGTYPE_VOIDPTR;
      spec->formatter = format_file;
      return 1;
    }
  }

  if (resolve_integral_type(spec)) {
    return 1;
  }

  if (spec->type_len == 1) {
    switch (spec->type[0]) {
      case 'F': spec->flags |= FMT_FLAG_UPPER; // fallthrough
      case 'f': spec->argtype = FMT_ARGTYPE_DOUBLE; spec->formatter = format_double; return 1;
      case 'E': spec->flags |= FMT_FLAG_UPPER; // fallthrough
      case 'e': spec->argtype = FMT_ARGTYPE_DOUBLE; spec->formatter = format_scientific; return 1;
      case 'G': spec->flags |= FMT_FLAG_UPPER; // fallthrough
      case 'g': spec->argtype = FMT_ARGTYPE_DOUBLE; spec->formatter = format_general; return 1;
      case 'A': spec->flags |= FMT_FLAG_UPPER; // fallthrough
      case 'a': spec->argtype = FMT_ARGTYPE_DOUBLE; spec->formatter = format_hex_float; return 1;
      case 's': spec->argtype = FMT_ARGTYPE_VOIDPTR; spec->formatter = format_string; return 1;
      case 'c': spec->argtype = FMT_ARGTYPE_INT32; spec->formatter = format_char; return 1;
      case 'p': spec->flags |= FMT_FLAG_ALT;
                spec->argtype = FMT_ARGTYPE_VOIDPTR; spec->formatter = format_hex; return 1;
      case 'M': spec->flags |= FMT_FLAG_UPPER;
                spec->argtype = FMT_ARGTYPE_SIZE; spec->formatter = format_mem_quantity; return 1;
      default: break;
    }
  }

  // type not found
  spec->argtype = FMT_ARGTYPE_NONE;
  spec->formatter = NULL;
  return 0;
}

size_t fmtlib_parse_printf_type(const char *format, const char **end) {
  // %[flags][width][.precision]type
  //    `                       ^ format
  const char *ptr = format;
  if (*ptr == 0) {
    *end = ptr;
    return 0;
  }

  switch (*ptr) {
    case 'd': case 'i': case 'u': case 'b':
    case 'o': case 'x': case 'X':
    case 'f': case 'F': case 'e': case 'E':
    case 'g': case 'G': case 'a': case 'A':
    case 's': case 'c': case 'p':
    case 'M':
      *end = ptr + 1;
      return 1;
    case 'l':
      if (ptr[1] == 'd' || ptr[1] == 'i' || ptr[1] == 'u' || ptr[1] == 'b' ||
          ptr[1] == 'o' || ptr[1] == 'x' || ptr[1] == 'X') {
        *end = ptr + 2;
        return 2;
      }
      if (ptr[1] == 'l') {
        if (ptr[2] == 'd' || ptr[2] == 'i' || ptr[2] == 'u' || ptr[2] == 'b' ||
            ptr[2] == 'o' || ptr[2] == 'x' || ptr[2] == 'X') {
          *end = ptr + 3;
          return 3;
        }
      }
      break;
    case 'L':
      if (ptr[1] == 'o') {
        *end = ptr + 2;
        return 2;
      }
      break;
    case 't':
      if (ptr[1] == 'd') {
        *end = ptr + 2;
        return 2;
      }
      break;
    case 'z':
      if (ptr[1] == 'd' || ptr[1] == 'i' || ptr[1] == 'u' || ptr[1] == 'b' ||
          ptr[1] == 'o' || ptr[1] == 'x' || ptr[1] == 'X') {
        *end = ptr + 2;
        return 2;
      }
      break;
    case 'v':
      if (ptr[1] == 'a' || ptr[1] == 'e' || ptr[1] == 'n' || ptr[1] == 't') {
        *end = ptr + 2;
        return 2;
      }
      break;
    default:
      break;
  }

  *end = format;
  return 0;
}

size_t fmtlib_format_spec(fmt_buffer_t *buffer, fmt_spec_t *spec) {
  // posix flag interactions
  if (spec->flags & FMT_FLAG_PRINTF) {
    // + overrides space
    if ((spec->flags & FMT_FLAG_SIGN) && (spec->flags & FMT_FLAG_SPACE)) {
      spec->flags &= ~FMT_FLAG_SPACE;
    }
    // for integer conversions, 0 flag is ignored when precision is specified
    if (spec->precision >= 0 && (spec->flags & FMT_FLAG_ZERO)) {
      bool is_integer = (spec->formatter == format_signed || spec->formatter == format_unsigned ||
                         spec->formatter == format_octal || spec->formatter == format_hex ||
                         spec->formatter == format_binary);
      if (is_integer) {
        spec->flags &= ~FMT_FLAG_ZERO;
        spec->fill_char = ' ';
      }
    }
  }

  if (spec->type_len == 0) {
    // no type specified, just apply alignment/padding
    return apply_alignment(buffer, spec, "", 0);
  } else if (spec->formatter == NULL) {
    return 0;
  }

  // if width is specified, we need to format the value into a temporary buffer
  // and then apply alignment/padding. otherwise we can just format directly
  // into the output buffer. this means that format strings specifying a width
  // are limited to FMTLIB_MAX_WIDTH characters.
  if (spec->width > 0) {
    char value_data[TEMP_BUFFER_SIZE];
    fmt_buffer_t value = fmtlib_buffer(value_data, TEMP_BUFFER_SIZE);

    size_t n = spec->formatter(&value, spec);
    return apply_alignment(buffer, spec, value_data, n);
  }
  return spec->formatter(buffer, spec);
}
