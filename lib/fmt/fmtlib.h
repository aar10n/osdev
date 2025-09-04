//
// Copyright (c) Aaron Gill-Braun. All rights reserved.
// Distributed under the terms of the MIT License. See LICENSE for details.
//

#ifndef LIB_FMT_FMTLIB_H
#define LIB_FMT_FMTLIB_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <kernel/string.h>

// determines the maximum width that can be specified
#define FMTLIB_MAX_WIDTH 256

// determines the maximum allowed length of a specifier type name.
#define FMTLIB_MAX_TYPE_LEN 16

// -----------------------------------------------------------------------------

#define FMT_FLAG_ALT    0x01 // alternate form
#define FMT_FLAG_UPPER  0x02 // uppercase form
#define FMT_FLAG_SIGN   0x04 // always print sign for numeric values
#define FMT_FLAG_SPACE  0x08 // leave a space in front of positive numeric values
#define FMT_FLAG_ZERO   0x10 // pad to width with leading zeros and keeps sign in front

typedef enum fmt_align {
  FMT_ALIGN_LEFT,
  FMT_ALIGN_CENTER,
  FMT_ALIGN_RIGHT,
} fmt_align_t;

typedef enum fmt_argtype {
  FMT_ARGTYPE_NONE,
  FMT_ARGTYPE_INT32,
  FMT_ARGTYPE_INT64,
  FMT_ARGTYPE_DOUBLE,
  FMT_ARGTYPE_SIZE,
  FMT_ARGTYPE_VOIDPTR,
} fmt_argtype_t;

typedef union fmt_raw_value {
  uint64_t uint64_value;
  double double_value;
  void *voidptr_value;
} fmt_raw_value_t;
#define fmt_rawvalue_uint64(v) ((union fmt_raw_value) { .uint64_value = (v) })
#define fmt_rawvalue_double(v) ((union fmt_raw_value) { .double_value = (v) })
#define fmt_rawvalue_voidptr(v) ((union fmt_raw_value) { .voidptr_value = (v) })

typedef struct fmt_spec fmt_spec_t;
typedef struct fmt_buffer fmt_buffer_t;

/// A function which writes a string to the buffer formatted according to the given specifier.
typedef size_t (*fmt_formatter_t)(fmt_buffer_t *buffer, const fmt_spec_t *spec);

/// Represents a fully-formed format specifier.
typedef struct fmt_spec {
  char type[FMTLIB_MAX_TYPE_LEN + 1];
  size_t type_len;
  int flags;
  int width;
  int precision;
  fmt_align_t align;
  char fill_char;
  const char *end;
  //
  fmt_raw_value_t value;
  fmt_argtype_t argtype;
  fmt_formatter_t formatter;
} fmt_spec_t;

// MARK: fmt_buffer_t API
// ======================
// This simple struct is used to safely bounds-check all writes to the buffer.

typedef struct fmt_buffer {
  char *data;
  size_t size;
  size_t written;
} fmt_buffer_t;

static inline fmt_buffer_t fmtlib_buffer(char *data, size_t size) {
  memset(data, 0, size);
  return (fmt_buffer_t) {
    .data = data,
    .size = size - 1, // null terminator
  };
}

static inline bool fmtlib_buffer_full(fmt_buffer_t *b) {
  return b->size == 0;
}

static inline size_t fmtlib_buffer_write(fmt_buffer_t *b, const char *data, size_t size) {
  size_t n = size < b->size ? size : b->size;
  if (b->size == 0) {
    b->written += n;
    return 0;
  }
  memcpy(b->data, data, n);
  b->data += n;
  b->size -= n;
  b->written += n;
  return n;
}

static inline size_t fmtlib_buffer_write_char(fmt_buffer_t *b, char c) {
  if (b->size == 0) {
    b->written++;
    return 0;
  }
  *b->data = c;
  b->data++;
  b->size--;
  b->written++;
  return 1;
}

// -----------------------------------------------------------------------------

/**
 * Resolves the specifier type to a formatter function and argument type.
 * If the format type exists, spec->formatter and spec->argtype will be set
 * and the function will return 1, otherwise 0 will be returned.
 *
 * @param type The type name
 * @param spec The format specifier
 * @return 1 on success, 0 on failure
 */
int fmtlib_resolve_type(fmt_spec_t *spec);

/**
 * Parses a type within a printf-style specifier.
 *
 *  %x
 *   ^- format
 *
 * @param format A pointer to the start of the type.
 * @param [out] end A pointer to a const char* which will be set to the end of the type.
 * @return The length of the type or 0 if the type is not valid.
 */
size_t fmtlib_parse_printf_type(const char *format, const char **end);

/**
 * Formats a value according to the given specifier.
 *
 * @param buffer the buffer to write the formatted value to
 * @param spec the specifier
 * @return the number of bytes written
 */
size_t fmtlib_format_spec(fmt_buffer_t *buffer, fmt_spec_t *spec);

#endif
