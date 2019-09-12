//
// Created by Aaron Gill-Braun on 2019-04-21.
//

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <drivers/screen.h>
#include <drivers/serial.h>
#include <stdbool.h>

#define BUFSIZE 1024

#define next_char() format++;
#define advance(index, fmt) \
  pos += index;             \
  format += fmt;
#define append_current() \
  string[pos] = *format; \
  advance(1, 1);
#define append_char(ch) \
  string[pos] = ch;     \
  advance(1, 1);
#define append_buffer()    \
  buffer[index] = *format; \
  index++;

#define peek() (*(format + 1))
#define is_repeat() (peek() == (*format))

#define setflag(flag) (flag = 1)
#define unsetflag(flag) (flag = 0)

#define apply_format(format, value)       \
  fmt_info.alt_form = flag_alt_form;      \
  fmt_info.pad_zero = flag_pad_zero;      \
  fmt_info.pad_right = flag_pad_right;    \
  fmt_info.add_space = flag_add_space;    \
  fmt_info.add_sign = flag_add_sign;      \
                                          \
  fmt_info.is_unsigned = is_unsigned;     \
  fmt_info.is_uppercase = is_uppercase;   \
  fmt_info.is_scientific = is_scientific; \
                                          \
  fmt_info.radix = radix;                 \
  fmt_info.width = width;                 \
  fmt_info.precision = precision;         \
  format(value, temp, &fmt_info);         \
  len = strlen(temp);                     \
  memcpy(string + pos, temp, len);        \
  advance(len, 1);


typedef struct {
  // Flags
  uint16_t alt_form : 1;  // Use the alternate for numbers
  uint16_t pad_zero : 1;  // Pad with zeros instead of spaces
  uint16_t pad_right : 1; // Padding is applied to the right
  uint16_t add_space : 1; // Add a space if there is no sign
  uint16_t add_sign : 1;  // Add plus sign if positive number

  // Length
  // uint16_t is_char       : 1; //
  // uint16_t is_short      : 1; //
  // uint16_t is_long       : 1; //
  // uint16_t is_longlong   : 1; //

  uint16_t is_unsigned : 1;   // Value is unsigned
  uint16_t is_uppercase : 1;  // Use uppercase for letters
  uint16_t is_scientific : 1; // Use scientific notation

  // Options
  uint8_t radix;      // Radix for conversion
  uint16_t width;     // Width of the value
  uint16_t precision; // Precision of the value
} format_t;

//
//
//

int add_prefix(char *s, int r) {
  switch (r) {
    case 2:
      s[0] = 'b';
      s[1] = '0';
      return 2;
    case 8:
      s[0] = '0';
      return 1;
    case 10:
      return 0;
    case 16:
      s[0] = 'x';
      s[1] = '0';
      return 2;
    default:
      return 0;
  }
}

// dtoc - Digit to Char
char _dtoc(int d, int r) {
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

// itoa - Int to ASCII
void _itoa(int n, char *s, format_t *f) {
  int i = 0;
  int sign;

  if (f->is_unsigned) {
    unsigned int un = (unsigned int) n;

    do {
      uint8_t digit = (un % f->radix);
      uint8_t ch = _dtoc(digit, f->radix);
      if (!f->is_uppercase && (ch >= 'A' && ch <= 'F')) ch += 32;
      s[i++] = ch;
    } while ((un /= f->radix) > 0);

    if (f->alt_form) i += add_prefix(s + i, f->radix);
  } else {
    if ((sign = n) < 0) n = -n;

    do {
      uint8_t digit = (n % f->radix);
      uint8_t ch = _dtoc(digit, f->radix);
      if (!f->is_uppercase && (ch >= 'A' && ch <= 'F')) ch += 32;
      s[i++] = ch;
    } while ((n /= f->radix) > 0);

    if (f->alt_form) i += add_prefix(s + i, f->radix);
    if (sign < 0) s[i++] = '-';
  }

  s[i] = '\0';
  reverse(s);
}

void _dtoa(double n, char *str, format_t *f) {}

//
//
//

/*
 * kprintf - write formatted output
 * ================================
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
  char string[BUFSIZE];
  char temp[64];
  char buffer[16];

  int len = 0;
  int pos = 0;
  int index = 0;
  int radix = 10;
  int width = 0;
  int precision = 0;

  bool is_uppercase = false;
  bool is_unsigned = false;
  bool is_scientific = false;

  // Flags
  bool flag_alt_form = false;
  bool flag_pad_zero = false;
  bool flag_pad_right = false;
  bool flag_add_space = false;
  bool flag_add_sign = false;

  // Length
  bool length_hh;
  bool length_h;
  bool length_l;
  bool length_ll;
  bool length_z;

  memset(string, 0, BUFSIZE);
  memset(temp, 0, 32);
  memset(buffer, 0, 8);

  format_t fmt_info;
  va_list valist;
  va_start(valist, format);
start:
  switch (*format) {
    case '\0':
      append_char('\0');
      goto end;
    case '%':
      next_char();
      goto flags;
    default:
      append_current();
      goto start;
  }


flags:
  // Flags
  switch (*format) {
    case '#':
      setflag(flag_alt_form);
      next_char();
      goto flags;
    case '0':
      setflag(flag_pad_zero);
      next_char();
      goto flags;
    case '-':
      setflag(flag_pad_right);
      next_char();
      goto flags;
    case ' ':
      setflag(flag_add_space);
      next_char();
      goto flags;
    case '+':
      setflag(flag_add_sign);
      next_char();
      goto flags;
    default:
      goto width;
  }

width:
  // Width
  switch (*format) {
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
      append_buffer();
      goto width;
    default:
      goto length;
  }

  // precision:

length:
  // Length
  switch (*format) {
    case 'h':
      is_repeat() ? setflag(length_hh) : setflag(length_h);
      next_char();
      goto type;
    case 'l':
      is_repeat() ? setflag(length_ll) : setflag(length_l);
      next_char();
      goto type;
    case 'z':
      setflag(length_z);
      next_char();
      goto type;
    default:
      goto type;
  }

type:
  // Type
  switch (*format) {
    case 'd':
    case 'i':
      // Decimal
      radix = 10;
      unsetflag(is_unsigned);
      goto format_int;
    case 'b':
      // Binary
      radix = 2;
      setflag(is_unsigned);
      setflag(flag_alt_form);
      goto format_int;
    case 'o':
      // Octal
      radix = 8;
      setflag(is_unsigned);
      goto format_int;
    case 'u':
      // Unsigned Decimal
      radix = 10;
      setflag(is_unsigned);
      goto format_int;
    case 'X':
      setflag(is_uppercase);
    case 'x':
      // Hexadecimal
      radix = 16;
      setflag(is_unsigned);
      goto format_int;
    case 'E':
      setflag(is_uppercase);
    case 'e':
      // Scientific
      setflag(is_scientific);
      goto format_int;
    case 'F':
      setflag(is_uppercase);
    case 'f':
      // Float
      radix = 10;
      goto format_float;
    case 'c':
      // Character
      goto format_char;
    case 's':
      // String
      goto format_string;
    case 'p':
      // Pointer
      radix = 16;
      setflag(is_unsigned);
      setflag(is_uppercase);
      setflag(flag_alt_form);
      goto format_int;
    case '%':
      append_current();
      goto start;
    default:
      // Error
      goto end;
  }

  // Formats
format_int:
  NULL;
  int dec = va_arg(valist, int);
  apply_format(_itoa, dec);
  goto start;

format_float:
  NULL;
  double fp = va_arg(valist, double);
  apply_format(_dtoa, fp);
  goto start;

format_char:
  NULL;
  char char_value = va_arg(valist, int);
  append_char(char_value);
  goto start;

format_string:
  NULL;
  char *str = va_arg(valist, char *);
  len = strlen(str);
  memcpy(string + pos, str, len);
  advance(len, 1);
  goto start;

end:
  va_end(valist);
  kputs(string);
  serial_write(COM1, string);
}
