//
// Copyright (c) Aaron Gill-Braun. All rights reserved.
// Distributed under the terms of the MIT License. See LICENSE for details.
//

#ifndef LIB_FMT_FMT_H
#define LIB_FMT_FMT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

#include "fmtlib.h"

// determines the maximum number of arguments that can be passed to fmt_format.
// extra arguments are ignored. the `max_args` parameter to fmt_format is clamped
// to this value.
#define FMT_MAX_ARGS 10

// determines the maximum number of format specifiers that can be used in a single
// format string. note that the formatting is still limited by FMT_MAX_ARGS but as
// long as specifiers do not implicitly consume arguments beyond this, they are
// allowed up to this limit.
#define FMT_MAX_SPECS 30


// -----------------------------------------------------------------------------

/*
 * Format Strings
 * ==============
 *
 * A format string is a string that contains zero or more format specifiers. A specifier
 * is a sequence of characters enclosed between '{' and '}', but printf style specifiers
 * using '%' are also supported. To specify a literal '{' use '{{' and for '}', use '}'
 * or '}}'.
 * The overall syntax of a format specifier is:
 *
 *     {[index]:[[$fill]align][flags][width][.precision][type]}
 *
 * Printf style specifiers are supported as well:
 *
 *     %[flags][width][.precision]type
 *
 * index
 *     The index field is an optional positive integer that specifies the index of
 *     the argument to use for the value. Implicitly assigned indices begin at the
 *     first argument (0) and are incremented by 1 for each argument that is not
 *     explicitly assigned an index.
 *
 * align
 *     The align field is an optional character that specifies the alignment of the output
 *     within the width of the field. A sequence of a '$' followed by a single non-null
 *     character may immediately precede the alignment marker to specify the character
 *     used for padding. If no alignment is specified, the output is left aligned using
 *     spaces. The following alignments are supported:
 *
 *         '[$fill]<' - left justify
 *         '[$fill]^' - center justify
 *         '[$fill]>' - right justify
 *
 * flags
 *     The flags field is a set of optional flags that modify the output.
 *     The following flags are supported:
 *
 *         '#'       - alternate form
 *         '!'       - uppercase form
 *         '0'       - sets the fill character to '0'
 *                     for numeric values, pad with leading zeros up to width (conflicts with `align`)
 *         '+'       - always print sign for numeric values
 *         '-'       - right align (overrides align and zero)
 *         ' '       - leave a space in front of positive numeric values (conflicts with '+')
 *
 * width
 *     The width field is an optional positive integer that specifies the minimum width
 *     of the output. After all other formatting is applied, the output is padded to the
 *     specified width using spaces or the fill character if specified in the align field.
 *
 *     The width may also be specified using a '*' which will cause the next implicit argument
 *     to be used as the width, or as '*index' where index is a positive integer, which will
 *     use the specified argument as the width. When using the '*' syntax, the argument must
 *     be an integer.
 *
 * precision
 *     The precision field is an optional positive integer.
 *     For floating point numbers, it specifies the number of digits to display after the
 *     decimal point. The default precision is 6 and the maximum precision is 9. The output
 *     is padded with trailing zeros if necessary.
 *     For integers, it specifies the minimum number of digits to display. By default, there
 *     is no minimum number of digits. The output is padded with leading zeros if necessary.
 *     For strings, it specifies the maximum number of characters to display. By default,
 *     strings are read until the first null character is found, but the precision field can
 *     be used to limit the number of characters read.
 *
 *     The precision may be specified using a '*' or '*index' as described in the width field.
 *
 * type
 *     The type field is an optional character or string that specifies the type of the
 *     argument. If no type is specified, the width and fill are respected, but no other
 *     formatting is applied.
 *     The following built-in types are supported:
 *
 *         '[<type>]d'   - signed decimal integer
 *         '[<type>]u'   - unsigned decimal integer
 *         '[<type>]b'   - unsigned binary integer
 *         '[<type>]o'   - unsigned octal integer
 *         '[<type>]x'   - unsigned hexadecimal integer
 *         where <type> is one of the following:
 *           'll' - 64-bit integer
 *           'z'  - size_t
 *         or a 32-bit integer if no type is specified
 *
 *
 *         'f'             - floating point number (double)
 *         'F'             - floating point number capitalized
 *
 *         's'             - string
 *         'c'             - character
 *         'p'             - pointer
 *
 *         'M'             - memory quantity
 *
 *         'Lo'            - pointer to struct lock_object
 *
 *         'pr'            - pointer to struct proc
 *         'td'            - pointer to struct thread
 *
 *         'va'            - pointer to struct vattr
 *         've'            - pointer to struct ventry
 *         'vn'            - pointer to struct vnode
 *         'vt'            - enum vtype
 *
 *         'err'           - error code as string
 *         'str'           - pointer to str_t
 *         'cstr'          - pointer to cstr_t
 *         'path'          - pointer to path_t
 *         'time'          - pointer to struct tm (UTC timestamp)
 *         'epoc'          - uint64_t (UNIX timestamp)
 *         'file'          - pointer to struct file
 *
 * Notes:
 *
 *   - The maximum number of arguments supported by the fmt funcions is defined by the
 *     `FMT_MAX_ARGS` macro.
 *   - Implicit arguments are limited to `max_args` (default FMT_MAX_ARGS) and will ignore
 *     any specifiers which consume further arguments.
 *
 * Examples:
 *     {:d}      - integer
 *     {:05d}    - integer, sign-aware zero padding
 *     {:.2f}    - double, 2 decimal places
 *     {:>10u}   - unsigned, right justified with spaces
 *     {:$#^10d} - integer, center justified with '#'
 *     {:s}      - string
 *     {:.3s}    - string of specific length
 *
 */
size_t fmt_format(const char *format, char *buffer, size_t size, int max_args, va_list args);

/**
 * Writes a formatted string to the given fmt_buffer.
 *
 * @param buffer the buffer to write to
 * @param format the format string
 * @param ...
 * @return the number of bytes written to the buffer
 */
size_t fmt_write(fmt_buffer_t *buffer, const char *format, ...);

#endif
