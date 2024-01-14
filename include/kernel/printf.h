//
// Created by Aaron Gill-Braun on 2021-03-07.
//

#define __PRINTF__
#ifndef KERNEL_PRINTF_H
#define KERNEL_PRINTF_H

#include <kernel/base.h>
#include <stdarg.h>

void kprintf_early_init();
void kprintf_kputs(const char *str);

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
 *         'f'             - floating point number (double)
 *         'F'             - floating point number capitalized
 *
 *         's'             - string
 *         'c'             - character
 *         'p'             - pointer
 *
 *         'M'             - memory quantity
 *
 *  not supported in printf style specifiers:
 *
 *         'err'           - error code as string
 *         'str'           - pointer to str_t
 *         'cstr'          - pointer to cstr_t
 *         'path'          - pointer to path_t
 *
 * https://github.com/aar10n/fmt_c
 */
void kprintf(const char *format, ...);
void kvfprintf(const char *format, va_list args);

/**
 * Write formatted data to a buffer.
 */
size_t ksprintf(char *str, const char *format, ...);
size_t kvsprintf(char *str, const char *format, va_list args);

/**
 * Write formatted data to a sized buffer.
 */
size_t ksnprintf(char *str, size_t n, const char *format, ...);
size_t kvsnprintf(char *str, size_t n, const char *format, va_list args);

/**
 * Write formatted data to an allocated string.
 *
 * This does not support strings longer than 512 characters.
 * It is the callers responsibility to free the allocated
 * buffer.
 */
char *kasprintf(const char *format, ...);
char *kvasprintf(const char *format, va_list args);

/**
 * Writes formatted data to a file (by path).
 *
 * If an error occurs while opening or writing to the write, the error is returned.
 * Otherwise 0 is returned on success.
 */
int kfprintf(const char *path, const char *format, ...);

/**
 * Writes formatted data to a file descriptor.
 *
 * If an error occurs during the write, the error is returned.
 * Otherwise 0 is returned on success. The file descriptor is
 * not closed on return.
 */
int kfdprintf(int fd, const char *format, ...);

#endif
