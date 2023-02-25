//
// Created by Aaron Gill-Braun on 2021-03-07.
//

#ifndef LIB_FORMAT_H
#define LIB_FORMAT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

/*
 * print_format - format a string using printf-like syntax
 * =======================================================
 *
 * print_format(const char *format, char *buffer, size_t size, va_list args, bool limit);
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
 *
 * Field Width
 *   An optional number specifying the minimum width of the
 *   converted value. If the converted value is smaller than
 *   the given width, it will be padded with spaces, or zeros
 *   if the '0' flag is used. By default the padding is from
 *   the left side, but can be changed with the '-' flag.
 *   In the case of 's' formatting, the width is the maximum
 *   number of characters to be printed from the string.
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
 *   'f' - Floating point (lowercase)
 *   'F' - Floating point (uppercase)
 *   'c' - Character
 *   's' - String
 *   'T' - path_t path string
 *   'p' - Pointer address
 *   'm' - Memory quantity (lowercase)
 *   'M' - Memory quantity (uppercase)
 *   'n' - Number of characters printed
 *   '%' - A '%' literal
 *
 */
int print_format(const char *format, char *str, size_t size, va_list args, bool limit);

#endif
