#include <stdlib.h>
#include <stdint.h>
#include <string.h>

// dtoc - Digit to Char
char dtoc(int d, int b) {
  switch (b) {
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

//
//
// Public API Functions
//
//

// atoi - ASCII to Int
int atoi(char *nptr) {
  int val = 0;
  int neg = 0;

  if (*nptr == '-') {
    neg = 1;
    nptr++;
  }

  while (*nptr) {
    val = val * 10 + (*nptr) - '0';
    nptr++;
  }

  return neg ? -val : val;
}

// itoa - Int to ASCII
void itoa(int n, char *s, int radix) {
  int i = 0;
  int sign;

  if ((sign = n) < 0) n = -n;

  do {
    uint8_t digit = (n % radix);
    s[i++] = dtoc(digit, radix);
  } while ((n /= radix) > 0);

  if (sign < 0) s[i++] = '-';
  s[i] = '\0';

  reverse(s);
}

// btoa - Binary to ASCII
void btoa(int n, char *s) {
  itoa(n, s, 2);
}

// dtoa - Decimal to ASCII
void dtoa(int n, char *s) {
  itoa(n, s, 10);
}

// xtoa - Hex to ASCII
void xtoa(int n, char *s) {
  itoa(n, s, 16);
}
