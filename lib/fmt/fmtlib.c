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
#include <kernel/vfs/path.h>


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
  .argtype = FMT_ARGTYPE_INT64,               \
  .value = fmt_rawvalue_uint64(v),            \
})

#define mk_fmtlib_spec_voidp(v) ((fmt_spec_t) { \
  .argtype = FMT_ARGTYPE_VOIDPTR,              \
  .value = fmt_rawvalue_voidptr(v),            \
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

// Writes a signed or unsigned number to the buffer using the given format.
static inline size_t format_integer(fmt_buffer_t *buffer, const fmt_spec_t *spec, bool is_signed, const struct num_format *format) {
  int width = min(max(spec->width, 0), FMTLIB_MAX_WIDTH);
  size_t n = 0;
  uint64_t v;
  bool is_negative = false;
  if (is_signed) {
    int64_t i = (int64_t) spec->value.uint64_value;
    if (i < 0) {
      v = -i;
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

  // pad with leading zeros to reach specified precision
  if ((size_t)spec->precision > len) {
    size_t padding = spec->precision - len;
    for (size_t i = 0; i < padding; i++) {
      n += fmtlib_buffer_write_char(buffer, '0');
    }
  }

  // left-pad number with zeros to reach specified width
  if (spec->flags & FMT_FLAG_ZERO && (size_t)width > len + n) {
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
// respects numeric flags. also supports the ALT flag for truncated
// representations of whole numbers (e.g. 1.000000 -> 1).
static inline size_t format_double(fmt_buffer_t *buffer, const fmt_spec_t *spec) {
  union double_raw v = { .value = spec->value.double_value };
  size_t width = (size_t) min(max(spec->width, 0), FMTLIB_MAX_WIDTH);
  size_t prec = (size_t) min((spec->precision > 0 ? spec->precision : PRECISION_DEFAULT), PRECISION_MAX);
  size_t n = 0;

  // write sign or space to buffer
  if (v.sign) {
    n += fmtlib_buffer_write_char(buffer, '-');
  } if (spec->flags & FMT_FLAG_SIGN) {
    n += fmtlib_buffer_write_char(buffer, '+');
  } else if (spec->flags & FMT_FLAG_SPACE) {
    n += fmtlib_buffer_write_char(buffer, ' ');
  }

  // handle special encodings
  if (v.exp == 0x7FF && v.frac == 0) {
    // infinity
    const char *inf = spec->flags & FMT_FLAG_UPPER ? "INF" : "inf";
    n += fmtlib_buffer_write(buffer, inf, 3);
    return n;
  } else if (v.exp == 0x7FF && v.frac != 0) {
    // NaN
    const char *nan = spec->flags & FMT_FLAG_UPPER ? "NAN" : "nan";
    n += fmtlib_buffer_write(buffer, nan, 3);
    return n;
  } else if (v.exp == 0 && v.frac == 0) {
    // zero
    n += fmtlib_buffer_write_char(buffer, '0');
    if (!(spec->flags & FMT_FLAG_ALT)) {
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

  // now to convert floating point numbers to strings we need to extract the whole
  // and fractional parts as integers. from there we simply convert each to a string
  // then write them to the buffer.
  uint64_t whole = (uint64_t) v.value;
  uint64_t frac;

  // shift the decimal point to the right by the specified precision
  double tmp = (v.value - (double)whole) * pow10[prec];
  frac = (uint64_t) tmp;

  // round the remaining fractional part
  double delta = tmp - (double)frac;
  if (delta > 0.5) {
    frac++;
    // handle rollover, e.g. case 0.99 with prec 1 is 1.0
    if (frac >= (uint64_t)pow10[prec]) {
      frac = 0;
      whole++;
    }
  } else if (delta < 0.5) {
    // do nothing
  } else if ((frac == 0) || (frac & 1)) {
    // if halfway, round up if odd or last digit is 0
    frac++;
  }

  // the only time we _dont_ want to write the decimal point and fraction is
  // when the fraction is zero while the ALT flag is set.
  bool write_decimal = !(frac == 0 && (spec->flags & FMT_FLAG_ALT));

  // write the whole part to the intermediate buffer
  char temp[TEMP_BUFFER_SIZE];
  size_t len = u64_to_str(whole, temp, &decimal_format);
  size_t frac_len = 0;
  if (write_decimal) {
    temp[len++] = '.';
    // write the fractional part to the intermediate buffer
    frac_len = u64_to_str(frac, temp + len, &decimal_format);
    len += frac_len;
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

  // now write the number to the buffer
  n += fmtlib_buffer_write(buffer, temp, len);

  // finally write the trailing zeros to the buffer
  if (write_decimal && prec > frac_len) {
    size_t padding = prec - frac_len;
    for (size_t i = 0; i < padding; i++) {
      n += fmtlib_buffer_write_char(buffer, '0');
    }
  }
  return n;
}

static size_t format_string(fmt_buffer_t *buffer, const fmt_spec_t *spec) {
  const char *str = spec->value.voidptr_value;
  size_t len = spec->precision;
  if (str == NULL) {
    str = "(null)";
    len = 6;
  } else if (len == 0) {
    len = strlen(str);
  }

  return fmtlib_buffer_write(buffer, str, len);
}

static size_t format_char(fmt_buffer_t *buffer, const fmt_spec_t *spec) {
  char c = *((char *)&spec->value);
  const char *str = &c;
  size_t len = 1;
  if (c == 0) {
    str = "\\0";
    len = 2;
  }
  return fmtlib_buffer_write(buffer, str, len);
}

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
  if (err >= 0) {
    return fmtlib_buffer_write(buffer, "success", 7);
  }

  const char *str = strerror(abs(err));
  if (str == NULL) {
    size_t n = fmtlib_buffer_write(buffer, "{unknown error: ", 16);
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
  // no strftime but just implement the format like: 2020-01-01 00:00:00
  char temp[TEMP_BUFFER_SIZE];
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
      for (size_t i = 0; i < padding / 2; i++) {
        n += fmtlib_buffer_write_char(buffer, pad_char);
      }
      n += fmtlib_buffer_write(buffer, str, len);
      for (size_t i = 0; i < padding - padding / 2; i++) {
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

  if (ptr[n] == 'l' && ptr[n+1] == 'l') {
    argtype = FMT_ARGTYPE_INT64;
    n += 2;
  } else if (ptr[n] == 'z') {
    argtype = FMT_ARGTYPE_SIZE;
    n += 1;
  } else {
    argtype = FMT_ARGTYPE_INT32;
  }

  switch (ptr[n]) {
    case 'd': formatter = format_signed; break;
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

  if (resolve_integral_type(spec)) {
    return 1;
  }

  if (spec->type_len == 1) {
    goto single_char;
  }

  // err -> int
  if (strncmp("err", spec->type, 3) == 0) {
    spec->argtype = FMT_ARGTYPE_INT32;
    spec->formatter = format_errno;
    return 1;
  }
  // str -> str_t*
  if (strncmp("str", spec->type, 3) == 0) {
    spec->argtype = FMT_ARGTYPE_VOIDPTR;
    spec->formatter = format_str_t;
    return 1;
  }
  // cstr -> cstr_t*
  if (strncmp("cstr", spec->type, 4) == 0) {
    spec->argtype = FMT_ARGTYPE_VOIDPTR;
    spec->formatter = format_cstr_t;
    return 1;
  }
  // path -> path_t*
  if (strncmp("path", spec->type, 4) == 0) {
    spec->argtype = FMT_ARGTYPE_VOIDPTR;
    spec->formatter = format_path_t;
    return 1;
  }
  // time -> struct tm*
  if (strncmp("time", spec->type, 4) == 0) {
    spec->argtype = FMT_ARGTYPE_VOIDPTR;
    spec->formatter = format_time_utc;
    return 1;
  }
  // epoc -> uint64_t
  if (strncmp("epoc", spec->type, 4) == 0) {
    spec->argtype = FMT_ARGTYPE_INT64;
    spec->formatter = format_time_unix;
    return 1;
  }

single_char:
  switch (spec->type[0]) {
    case 'F': spec->flags |= FMT_FLAG_UPPER; // fallthrough
    case 'f': spec->argtype = FMT_ARGTYPE_DOUBLE; spec->formatter = format_double; return 1;
    case 's': spec->argtype = FMT_ARGTYPE_VOIDPTR; spec->formatter = format_string; return 1;
    case 'c': spec->argtype = FMT_ARGTYPE_INT32; spec->formatter = format_char; return 1;
    case 'p': spec->flags |= FMT_FLAG_ALT;
              spec->argtype = FMT_ARGTYPE_VOIDPTR; spec->formatter = format_hex; return 1;
    case 'M': spec->flags |= FMT_FLAG_UPPER; // fallthrough
              spec->argtype = FMT_ARGTYPE_SIZE; spec->formatter = format_mem_quantity; return 1;
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
    case 'd': case 'u': case 'b':
    case 'o': case 'x': case 'X':
    case 'f': case 'F': case 's':
    case 'c': case 'p':
    case 'M':
      *end = ptr + 1;
      return 1;
    case 'l':
      if (ptr[1] == 'l') {
        if (ptr[2] == 'd' || ptr[2] == 'u' || ptr[2] == 'b' ||
            ptr[2] == 'o' || ptr[2] == 'x' || ptr[2] == 'X') {
          *end = ptr + 3;
          return 3;
        }
      }
      break;
    case 'z':
      if (ptr[1] == 'd' || ptr[1] == 'u' || ptr[1] == 'b' ||
          ptr[1] == 'o' || ptr[1] == 'x' || ptr[1] == 'X') {
        *end = ptr + 2;
        return 2;
      }
      break;
  }

  *end = format;
  return 0;
}

size_t fmtlib_format_spec(fmt_buffer_t *buffer, fmt_spec_t *spec) {
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
