//
// Copyright (c) Aaron Gill-Braun. All rights reserved.
// Distributed under the terms of the MIT License. See LICENSE for details.
//

#include "fmt.h"
#include "fmtlib.h"

#pragma clang diagnostic push
#pragma ide diagnostic ignored "OCDFAInspection"

#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define is_digit(ch) ((ch) >= '0' && (ch) <= '9')
#define is_alpha(ch) (((ch) >= 'a' && (ch) <= 'z') || ((ch) >= 'A' && (ch) <= 'Z'))
#define is_align(ch) ((ch) == '<' || (ch) == '^' || (ch) == '>')

typedef struct parsed_fmt_spec {
  int index;
  int flags;
  int width_or_index;
  bool width_is_index;
  int precision_or_index;
  bool precision_is_index;
  fmt_align_t align;
  char fill_char;
  const char *type;
  size_t type_len;
  bool valid;
  bool explicit_align;  // track if alignment was explicitly specified
} parsed_fmt_spec_t;

static inline int read_int(const char **ptr) {
  const char *start = *ptr;
  while (is_digit(**ptr)) {
    (*ptr)++;
  }

  size_t size = *ptr - start;
  int value = 0;
  int sign = 1;
  if (start[0] == '-') {
    sign = -1;
    start++;
    size--;
  }

  for (size_t i = 0; i < size; i++) {
    value *= 10;
    value += start[i] - '0';
  }
  return sign * value;
}

// parses fmt '{...}' specifiers
static inline size_t parse_fmt_spec(const char *format, int max_args, int *arg_index, int *arg_count, parsed_fmt_spec_t *spec) {
#define CHECK_MAX_ARGS(idx) ({ \
    if ((idx) >= max_args) {  \
      goto early_exit;            \
    }                             \
  })
#define CHECK_EOF(ptr) ({ \
    if (*(ptr) == '}')    \
      goto parse_type;    \
    if (*(ptr) == 0)      \
      goto early_exit;    \
  })

  if (*format != '{') {
    return 0;
  }

  // {[index]:[[$fill]align][flags][width][.precision][type]}
  // ^ format
  const char *start;
  const char *ptr = format + 1;

  int index = 0;
  int flags = 0;
  int width_or_index = 0;
  bool width_is_index = false;
  int precision_or_index = -1;
  bool precision_is_index = false;
  fmt_align_t align = FMT_ALIGN_LEFT;
  char fill_char = ' ';
  bool explicit_align = false;
  int new_arg_index = *arg_index;

  // ====== index ======
  CHECK_EOF(ptr);
  if (is_digit(*ptr)) {
    index = read_int(&ptr);
    CHECK_MAX_ARGS(index);
  } else {
    CHECK_MAX_ARGS(new_arg_index);
    index = new_arg_index;
    new_arg_index++;
  }

  if (*ptr == '}') {
    goto parse_type;
  } else if (*ptr == ':') {
    ptr++;
  } else {
    goto early_exit;
  }

  // quick check for fast path
  if (is_alpha(*ptr)) {
    goto parse_type;
  } else if (*ptr == '0') {
    goto parse_flags;
  } else if (is_digit(*ptr)) {
    goto parse_width;
  } else if (*ptr == '*' && *(ptr + 1) != 0 && !is_align(*(ptr + 1))) {
    // '*' is dynamic width only if not followed by an alignment char
    // (otherwise it's a fill character like {: *^5d})
    goto parse_width;
  } else if (*ptr == '.') {
    goto parse_precision;
  }

    // ====== align ======
  CHECK_EOF(ptr);

  // check for alignment with optional fill character
  // supports both: [fill]<align> and $[fill]<align> formats
  if (*ptr == '$') {
    // format: $[fill]<align>
    ptr++;
    if (*ptr == 0)
      goto early_exit;
    fill_char = *ptr++;
    if (!is_align(*ptr))
      goto early_exit;
  } else if (*ptr != 0 && *(ptr + 1) != 0 && is_align(*(ptr + 1))) {
    // format: [fill]<align>
    fill_char = *ptr++;
  }

  // parse the alignment character
  switch (*ptr) {
    case '<': align = FMT_ALIGN_LEFT; explicit_align = true; ptr++;
      break;
    case '^': align = FMT_ALIGN_CENTER; explicit_align = true; ptr++;
      break;
    case '>': align = FMT_ALIGN_RIGHT; explicit_align = true; ptr++;
      break;
  }

  // ====== flags ======
  CHECK_EOF(ptr);
  flags = 0;
parse_flags:
  switch (*ptr) {
    case '#': flags |= FMT_FLAG_ALT; ptr++;
      goto parse_flags;
    case '!': flags |= FMT_FLAG_UPPER; ptr++;
      goto parse_flags;
    case '0': flags |= FMT_FLAG_ZERO; fill_char = '0'; ptr++;
      goto parse_flags;
    case '+': flags |= FMT_FLAG_SIGN; ptr++;
      goto parse_flags;
    case '-': align = FMT_ALIGN_LEFT; explicit_align = true; ptr++; // left align flag
      flags &= ~FMT_FLAG_ZERO;
      goto parse_flags;
    case ' ': flags |= FMT_FLAG_SPACE; ptr++;
      goto parse_flags;
  }

  // ====== width ======
  CHECK_EOF(ptr);
parse_width:
  if (is_digit(*ptr)) {
    width_or_index = read_int(&ptr);
    width_is_index = false;
  } else if (*ptr == '*') {
    ptr++;
    if (*ptr == 0) {
      goto early_exit;
    } else if (is_digit(*ptr)) {
      width_or_index = read_int(&ptr);
      width_is_index = true;
      CHECK_MAX_ARGS(width_or_index);
    } else {
      CHECK_MAX_ARGS(new_arg_index);
      width_or_index = new_arg_index;
      width_is_index = true;
      new_arg_index++;
    }
  }

  // ====== precision ======
  CHECK_EOF(ptr);
parse_precision:
  if (*ptr == '.') {
    ptr++;
    if (is_digit(*ptr)) {
      precision_or_index = read_int(&ptr);
      precision_is_index = false;
    } else if (*ptr == '*') {
      ptr++;
      if (*ptr == 0) {
        goto early_exit;
      } else if (is_digit(*ptr)) {
        precision_or_index = read_int(&ptr);
        precision_is_index = true;
        CHECK_MAX_ARGS(precision_or_index);
      } else {
        CHECK_MAX_ARGS(new_arg_index);
        precision_or_index = new_arg_index;
        precision_is_index = true;
        new_arg_index++;
      }
    } else {
      goto early_exit;
    }
  }

  // ====== type ======
parse_type:
  start = ptr;
  while (*ptr && *ptr != '}') {
    ptr++;
  }
  if (*ptr == 0)
    goto early_exit;

  // ====== finish ======
  spec->index = index;
  spec->flags = flags;
  spec->width_or_index = width_or_index;
  spec->width_is_index = width_is_index;
  spec->precision_or_index = precision_or_index;
  spec->precision_is_index = precision_is_index;
  spec->align = align;
  spec->fill_char = fill_char;
  spec->type = start;
  spec->type_len = ptr - start;
  spec->valid = true;
  spec->explicit_align = explicit_align;

  int max_arg_index = index;
  if (width_is_index)
    max_arg_index = max(max_arg_index, width_or_index);
  if (precision_is_index)
    max_arg_index = max(max_arg_index, precision_or_index);

  *arg_count = max(*arg_count, max_arg_index + 1);
  *arg_index = new_arg_index;
  return ptr - format + 1;

  //
  // ERROR
early_exit:
  // something went wrong, write nothing and skip to end of format string
  start = format;
  while (*format && *format != '}') {
    format++;
  }

  spec->valid = false;
  return format - start + (*format == '}' ? 1 : 0);
#undef CHECK_EOF
#undef CHECK_MAX_ARGS
}

// parses printf '%...' specifiers with POSIX compliance
static inline size_t parse_printf_spec(const char *format, int max_args, int *arg_index, int *arg_count, bool *uses_positional_global, parsed_fmt_spec_t *spec) {
#define CHECK_MAX_ARGS(idx) ({ \
    if ((idx) >= max_args) {  \
      goto early_exit;            \
    }                             \
  })
#define CHECK_EOF(ptr) ({ \
    if (*(ptr) == 0)      \
      goto early_exit;    \
  })

  if (*format != '%') {
    return 0;
  }

  // %[position][flags][width][.precision][length]type
  // ^ format
  const char *end = format;
  const char *ptr = format + 1;

  int index;
  int flags = 0;
  int width_or_index = 0;
  bool width_is_index = false;
  int precision_or_index = -1;
  bool precision_is_index = false;
  fmt_align_t align = FMT_ALIGN_RIGHT; // printf defaults to right align
  char fill_char = ' ';
  bool explicit_align = false;
  int new_arg_index = *arg_index;
  bool uses_positional = false;

  // ====== positional parameters (n$) ======
  CHECK_EOF(ptr);
  const char *pos_start = ptr;
  while (is_digit(*ptr)) {
    ptr++;
  }
  if (*ptr == '$') {
    // positional parameter found
    uses_positional = true;
    *uses_positional_global = true;
    index = 0;
    for (const char *p = pos_start; p < ptr; p++) {
      index = index * 10 + (*p - '0');
    }
    index--; // convert to 0-based
    CHECK_MAX_ARGS(index);
    ptr++; // skip '$'
  } else if (*uses_positional_global) {
    // if format string uses positional, ALL specifiers must use positional
    goto early_exit;
  } else {
    // no positional, reset pointer and defer index assignment until after width/precision parsing
    ptr = pos_start;
    index = -1; // will be assigned later
  }

  // ====== flags ======
  CHECK_EOF(ptr);
  flags = 0;
parse_flags:
  switch (*ptr) {
    case '#': flags |= FMT_FLAG_ALT; ptr++;
      goto parse_flags;
    // '!' not in printf
    case '0': flags |= FMT_FLAG_ZERO; fill_char = '0'; ptr++;
      goto parse_flags;
    case '+': flags |= FMT_FLAG_SIGN; ptr++;
      goto parse_flags;
    case '-': align = FMT_ALIGN_LEFT; explicit_align = true; ptr++; // left justify in printf
      flags &= ~FMT_FLAG_ZERO;
      goto parse_flags;
    case ' ': flags |= FMT_FLAG_SPACE; ptr++;
      goto parse_flags;
  }

  // ====== width ======
  CHECK_EOF(ptr);
  if (is_digit(*ptr)) {
    width_or_index = read_int(&ptr);
    width_is_index = false;
  } else if (*ptr == '*') {
    ptr++;
    if (uses_positional) {
      // *n$ format for positional width
      if (is_digit(*ptr)) {
        width_or_index = read_int(&ptr);
        if (*ptr == '$') {
          ptr++;
          width_or_index--; // convert to 0-based
          width_is_index = true;
          CHECK_MAX_ARGS(width_or_index);
        } else {
          goto early_exit;
        }
      } else {
        goto early_exit;
      }
    } else {
      // simple * format
      CHECK_MAX_ARGS(new_arg_index);
      width_or_index = new_arg_index;
      width_is_index = true;
      new_arg_index++;
    }
  }

  // ====== precision ======
  CHECK_EOF(ptr);
  if (*ptr == '.') {
    ptr++;
    if (is_digit(*ptr)) {
      precision_or_index = read_int(&ptr);
      precision_is_index = false;
    } else if (*ptr == '*') {
      ptr++;
      if (uses_positional) {
        // .*n$ format for positional precision
        if (is_digit(*ptr)) {
          precision_or_index = read_int(&ptr);
          if (*ptr == '$') {
            ptr++;
            precision_or_index--; // convert to 0-based
            precision_is_index = true;
            CHECK_MAX_ARGS(precision_or_index);
          } else {
            goto early_exit;
          }
        } else {
          goto early_exit;
        }
      } else {
        // simple .* format
        CHECK_MAX_ARGS(new_arg_index);
        precision_or_index = new_arg_index;
        precision_is_index = true;
        new_arg_index++;
      }
    } else {
      // .0 precision (just a dot)
      precision_or_index = 0;
      precision_is_index = false;
    }
  }

  // assign value index for non-positional arguments (after width/precision parsing)
  if (index == -1) {
    CHECK_MAX_ARGS(new_arg_index);
    index = new_arg_index;
    new_arg_index++;
  }

  // we have built-in types like "lld" and "zx" which implicity encode the length
  // in a backward compatible-way so we don't need to parse the length specifier
  // separately.

  // ====== type ======
  if (!fmtlib_parse_printf_type(ptr, &end)) {
    ptr++;
    end = ptr;
    goto early_exit;
  }

  // ====== finish ======
  spec->index = index;
  spec->flags = flags;
  spec->width_or_index = width_or_index;
  spec->width_is_index = width_is_index;
  spec->precision_or_index = precision_or_index;
  spec->precision_is_index = precision_is_index;
  spec->align = align;
  spec->fill_char = fill_char;
  spec->explicit_align = explicit_align;
  spec->type = ptr;
  spec->type_len = end - ptr;
  spec->valid = true;

  int max_arg_index = index;
  if (width_is_index)
    max_arg_index = max(max_arg_index, width_or_index);
  if (precision_is_index)
    max_arg_index = max(max_arg_index, precision_or_index);

  *arg_count = max(*arg_count, max_arg_index + 1);
  if (!uses_positional) {
    *arg_index = new_arg_index;
  }
  return end - format;

  //
  // ERROR
early_exit:
  // something went wrong, bail out
  spec->valid = false;
  return end - format;
#undef CHECK_EOF
#undef CHECK_MAX_ARGS
}

//

size_t fmt_format(const char *format, char *buffer, size_t size, int max_args, va_list args) {
  va_list args_copy;
  va_copy(args_copy, args);

  size_t n = 0;
  fmt_buffer_t buf = fmtlib_buffer(buffer, size);

  // the formatter has two different modes of operation depending on the format string.
  // it always starts in single-pass mode, in which it writes to the buffer as it scans
  // the format string. the only time it switches to two-pass mode is when it encounters
  // a specifier that references an argument index greater than the number of arguments
  // read so far. in this case, we have to parse the rest of the format string to determine
  // the size of each argument, load them, and then write it all to the buffer.
  bool single_pass = true;

  // we keep three counters to track arguments. arg_index is used to track implicitly indexed
  // arguments, arg_count to track the largest argument referenced by a specifier and finally
  // loaded_arg_count which tracks the number of arguments read with va_arg. the last counter
  // is only used in two-pass mode because in single-pass mode arg_index == loaded_arg_count.
  // these counters are passed to the specifier parser function, which will track them internally
  // and then update the values through the pointers if the spec is valid.
  int arg_index = 0;
  int arg_count = 0;
  int loaded_arg_count = 0;
  fmt_raw_value_t values[FMT_MAX_ARGS] = {0};
  fmt_argtype_t argtypes[FMT_MAX_ARGS] = {0};

  // the following counter keeps track of specifiers when running in two-pass mode. in single-pass
  // mode specifiers are written directly to the buffer and so there is no limit on the number of
  // specifiers, as long as they dont reference more than FMT_MAX_ARGS arguments.
  int spec_index = 0;
  int pass_two_index;
  bool format_uses_positional = false; // track if any specifier uses positional syntax
  fmt_spec_t specs[FMT_MAX_SPECS] = {0};
  parsed_fmt_spec_t parsed_specs[FMT_MAX_SPECS] = {0};

  const char *ptr = format;
  const char *pass_two_start;
  while (*ptr && !fmtlib_buffer_full(&buf)) {
    // start of fmt specifier
    if (*ptr == '{' || *ptr == '%') {
      char format_char = *ptr;
      if ((format_char == '{' && *(ptr + 1) == '{') || (format_char == '%' && *(ptr + 1) == '%')) { // escaped
        if (single_pass)
          n += fmtlib_buffer_write_char(&buf, *ptr);

        ptr += 2;
        continue;
      }

      int cur_spec_index = spec_index;
      if (cur_spec_index >= FMT_MAX_SPECS)
        continue; // too many specifiers
      spec_index++;

      parsed_fmt_spec_t *parsed_spec = &parsed_specs[cur_spec_index];
      fmt_spec_t *spec = &specs[cur_spec_index];
      int saved_arg_index = arg_index;
      int saved_arg_count = arg_count;
      size_t m;
      if (format_char == '{') {
        m = parse_fmt_spec(ptr, max_args, &arg_index, &arg_count, parsed_spec);
      } else {
        m = parse_printf_spec(ptr, max_args, &arg_index, &arg_count, &format_uses_positional, parsed_spec);
        parsed_spec->flags |= FMT_FLAG_PRINTF;
      }
      spec->end = ptr + m;
      ptr += m;
      if (!parsed_spec->valid)
        continue;

      if (single_pass && arg_count > arg_index + 1) {
        // the spec references an argument index greater than what we've loaded so far
        // so we have to switch to two-pass mode
        single_pass = false;
        pass_two_start = ptr - m;
        pass_two_index = cur_spec_index;
      }

      size_t clamped_type_len = min(parsed_spec->type_len, FMTLIB_MAX_TYPE_LEN);
      memcpy(spec->type, parsed_spec->type, clamped_type_len);
      spec->type[clamped_type_len] = 0;
      spec->type_len = clamped_type_len;
      spec->value = fmt_rawvalue_uint64(0);
      spec->flags = parsed_spec->flags;
      spec->align = parsed_spec->align;
      spec->fill_char = parsed_spec->fill_char;
      spec->precision = 0; // initialize to default

      // resolve specifier type
      if (!fmtlib_resolve_type(spec)) {
        if (format_char == '{' && single_pass) {
          // invalid type
          n += fmtlib_buffer_write(&buf, "{bad type: ", 11);
          n += fmtlib_buffer_write(&buf, spec->type, parsed_spec->type_len);
          n += fmtlib_buffer_write_char(&buf, '}');
        }

        // mark invalid and roll back arg index/count - we can't safely
        // va_arg an unknown type without corrupting the va_list
        parsed_spec->valid = false;
        arg_index = saved_arg_index;
        arg_count = saved_arg_count;
        continue;
      }
      argtypes[parsed_spec->index] = spec->argtype;

      // set default alignment based on type if no explicit alignment was specified
      if (!parsed_spec->explicit_align) {
        switch (spec->argtype) {
          case FMT_ARGTYPE_INT32:
          case FMT_ARGTYPE_INT64:
          case FMT_ARGTYPE_SIZE:
          case FMT_ARGTYPE_DOUBLE:
            spec->align = FMT_ALIGN_RIGHT;  // numbers are right-aligned by default
            break;
          case FMT_ARGTYPE_VOIDPTR:
          case FMT_ARGTYPE_NONE:
          default:
            // keep left alignment for strings and others
            break;
        }
      }

      if (parsed_spec->width_is_index) {
        argtypes[parsed_spec->width_or_index] = FMT_ARGTYPE_INT32;
      } else {
        spec->width = parsed_spec->width_or_index;
      }
      if (parsed_spec->precision_is_index) {
        argtypes[parsed_spec->precision_or_index] = FMT_ARGTYPE_INT32;
      } else {
        spec->precision = parsed_spec->precision_or_index;
      }

      // handle %% literal
      if (spec->type_len == 1 && spec->type[0] == '%') {
        if (single_pass)
          n += fmtlib_buffer_write_char(&buf, '%');
        continue;
      }

      if (!single_pass) {
        continue;
      }

      // load argument(s)
      for (int i = loaded_arg_count; i < arg_count; i++) {
        switch (argtypes[i]) {
          case FMT_ARGTYPE_NONE: {
            // consume the argument even if unused (needed for positional args)
            int32_t val = va_arg(args_copy, int32_t);
            values[i] = fmt_rawvalue_uint64((uint64_t)val);
            break;
          }
          case FMT_ARGTYPE_INT32: {
            int32_t val = va_arg(args_copy, int32_t);
            values[i] = fmt_rawvalue_uint64((uint64_t)val);
            break;
          }
          case FMT_ARGTYPE_INT64: values[i] = fmt_rawvalue_uint64((uint64_t)va_arg(args_copy, int64_t)); break;
          case FMT_ARGTYPE_DOUBLE: values[i] = fmt_rawvalue_double(va_arg(args_copy, double)); break;
          case FMT_ARGTYPE_SIZE: values[i] = fmt_rawvalue_uint64((uint64_t)va_arg(args_copy, size_t)); break;
          case FMT_ARGTYPE_VOIDPTR: values[i] = fmt_rawvalue_voidptr(va_arg(args_copy, void*)); break;
        }
        loaded_arg_count++;
      }

      spec->value = values[parsed_spec->index];
      if (parsed_spec->width_is_index) {
        spec->width = (int) values[parsed_spec->width_or_index].uint64_value;
      }
      // posix: negative width is treated as '-' flag with positive width
      if (spec->width < 0) {
        spec->width = -spec->width;
        spec->align = FMT_ALIGN_LEFT;
      }
      if (parsed_spec->precision_is_index) {
        spec->precision = (int) values[parsed_spec->precision_or_index].uint64_value;
      } else {
        spec->precision = parsed_spec->precision_or_index;
      }
      // posix: negative precision is treated as if precision were omitted
      if (spec->precision < 0 && parsed_spec->precision_is_index) {
        spec->precision = -1;
      }

      // handle %% literal in single pass
      if (spec->type_len == 1 && spec->type[0] == '%') {
        n += fmtlib_buffer_write_char(&buf, '%');
        continue;
      }

      // =======================
      // SINGLE-PASS
      if (spec->argtype == FMT_ARGTYPE_NONE) {
        // no value
        n += fmtlib_format_spec(&buf, spec);
        continue;
      }

      // format
      n += fmtlib_format_spec(&buf, spec);
    } else if (*ptr == '}') {
      if (single_pass)
        n += fmtlib_buffer_write_char(&buf, '}');

      ptr++;
      if (*ptr == '}')
        ptr++; // skip extra to allow for balanced escaped braces
    } else {
        if (single_pass)
          n += fmtlib_buffer_write_char(&buf, *ptr);

        ptr++;
    }
  }

  if (single_pass) {
    va_end(args_copy);
    return n;
  }

  // =======================
  // DOUBLE-PASS

  // load argument(s)
  for (int i = loaded_arg_count; i < arg_count; i++) {
    switch (argtypes[i]) {
      case FMT_ARGTYPE_NONE: // consume as int (needed for positional args) // NOLINT(bugprone-branch-clone)
      case FMT_ARGTYPE_INT32: values[i] = fmt_rawvalue_uint64((uint64_t)va_arg(args_copy, int32_t)); break;
      case FMT_ARGTYPE_INT64: values[i] = fmt_rawvalue_uint64((uint64_t)va_arg(args_copy, int64_t)); break;
      case FMT_ARGTYPE_DOUBLE: values[i] = fmt_rawvalue_double(va_arg(args_copy, double)); break;
      case FMT_ARGTYPE_SIZE: values[i] = fmt_rawvalue_uint64((uint64_t)va_arg(args_copy, size_t)); break;
      case FMT_ARGTYPE_VOIDPTR: values[i] = fmt_rawvalue_voidptr(va_arg(args_copy, void*)); break;
    }
    loaded_arg_count++;
  }

  // now make a second pass over the format string to print it. this time we dont
  // have to reparse the specifiers
  ptr = pass_two_start;
  int index = pass_two_index;
  while (*ptr && !fmtlib_buffer_full(&buf) && index < spec_index) {
    if (*ptr == '{') {
      if (*(ptr + 1) == '{') { // escaped
        n += fmtlib_buffer_write_char(&buf, '{');
        ptr += 2;
        continue;
      }

      parsed_fmt_spec_t *parsed_spec = &parsed_specs[index];
      fmt_spec_t *spec = &specs[index];
      index++;
      if (!parsed_spec->valid)
        continue;

      // handle %% literal in two-pass mode
      if (spec->type_len == 1 && spec->type[0] == '%') {
        n += fmtlib_buffer_write_char(&buf, '%');
        ptr = spec->end;
        continue;
      }

      spec->value = values[parsed_spec->index];
      if (parsed_spec->width_is_index) {
        spec->width = (int) values[parsed_spec->width_or_index].uint64_value;
      }
      if (spec->width < 0) {
        spec->width = -spec->width;
        spec->align = FMT_ALIGN_LEFT;
      }
      if (parsed_spec->precision_is_index) {
        spec->precision = (int) values[parsed_spec->precision_or_index].uint64_value;
      } else {
        spec->precision = parsed_spec->precision_or_index;
      }
      if (spec->precision < 0 && parsed_spec->precision_is_index) {
        spec->precision = -1;
      }

      n += fmtlib_format_spec(&buf, spec);
      ptr = spec->end;
    } else if (*ptr == '%') {
      if (*(ptr + 1) == '%') { // escaped
        n += fmtlib_buffer_write_char(&buf, '%');
        ptr += 2;
        continue;
      }

      parsed_fmt_spec_t *parsed_spec = &parsed_specs[index];
      fmt_spec_t *spec = &specs[index];
      index++;
      if (!parsed_spec->valid)
        continue;

      // handle %% literal in two-pass mode
      if (spec->type_len == 1 && spec->type[0] == '%') {
        n += fmtlib_buffer_write_char(&buf, '%');
        ptr = spec->end;
        continue;
      }

      spec->value = values[parsed_spec->index];
      if (parsed_spec->width_is_index) {
        spec->width = (int) values[parsed_spec->width_or_index].uint64_value;
      } else {
        spec->width = parsed_spec->width_or_index;
      }
      if (spec->width < 0) {
        spec->width = -spec->width;
        spec->align = FMT_ALIGN_LEFT;
      }
      if (parsed_spec->precision_is_index) {
        spec->precision = (int) values[parsed_spec->precision_or_index].uint64_value;
      } else {
        spec->precision = parsed_spec->precision_or_index;
      }
      if (spec->precision < 0 && parsed_spec->precision_is_index) {
        spec->precision = -1;
      }

      n += fmtlib_format_spec(&buf, spec);
      ptr = spec->end;
    } else if (*ptr == '}') {
      n += fmtlib_buffer_write_char(&buf, '}');
      ptr++;
      if (*ptr == '}')
        ptr++;
    } else {
      n += fmtlib_buffer_write_char(&buf, *ptr);
      ptr++;
    }
  }

  while (*ptr && !fmtlib_buffer_full(&buf)) {
    n += fmtlib_buffer_write_char(&buf, *ptr);
    ptr++;
  }

  va_end(args_copy);
  return n;
}

size_t fmt_write(fmt_buffer_t *buffer, const char *format, ...) {
  va_list args;
  va_start(args, format);
  size_t n = fmt_format(format, buffer->data, buffer->size, FMT_MAX_ARGS, args);
  va_end(args);

  buffer->written += n;
  buffer->data += n;
  buffer->size -= n;
  return n;
}

#pragma clang diagnostic pop
