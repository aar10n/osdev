//
// Created by Aaron Gill-Braun on 2021-07-25.
//

#include <kernel/string.h>
#include <limits.h>
#include <kernel/panic.h>
#include <kernel/mm.h>

extern const char *errno_str[];

//

int memcmp(const void *str1, const void *str2, size_t count) {
  const unsigned char *s1 = str1;
  const unsigned char *s2 = str2;

  while (count-- > 0) {
    if (*s1++ != *s2++) {
      return s1[-1] < s2[-1] ? -1 : 1;
    }
  }
  return 0;
}

void *memcpy(void *dest, const void *src, size_t len) {
  if (len == 0) {
    return NULL;
  }

  char *d = dest;
  const char *s = src;
  while (len--) {
    *d++ = *s++;
  }
  return dest;
}

void *memmove(void *dest, const void *src, size_t len) {
  if (len == 0) {
    return dest;
  }

  char *d = dest;
  const char *s = src;
  if (d < s) {
    while (len--) {
      *d++ = *s++;
    }
  } else {
    char *lasts = s + (len - 1);
    char *lastd = d + (len - 1);
    while (len--) {
      *lastd-- = *lasts--;
    }
  }
  return dest;
}

void *memset(void *dest, int val, size_t len) {
  // return __memset8(dest, val, len);
  unsigned char *ptr = dest;
  while (len-- > 0) {
    *ptr++ = val;
  }
  return dest;
}

/*  */

int strcmp(const char *s1, const char *s2) {
  while (*s1) {
    if (*s1 != *s2) break;
    s1++;
    s2++;
  }

  return *s1 - *s2;
}

// Adapted from: https://github.com/gcc-mirror/gcc/blob/master/libiberty/strncmp.c
int strncmp(const char *s1, const char *s2, size_t n) {
  unsigned char u1, u2;

  while (n-- > 0) {
    u1 = (unsigned char) *s1++;
    u2 = (unsigned char) *s2++;
    if (u1 != u2) {
      return u1 - u2;
    }

    if (u1 == '\0') {
      return 0;
    }
  }
  return 0;
}

int strlen(const char *s) {
  int len = 0;
  while (*s != 0) {
    len++;
    s++;
  }
  return len;
}

void *strcpy(char *dest, const char *src) {
  size_t len = strlen(src);
  char *d = dest;
  const char *s = src;
  while (len--) {
    *d++ = *s++;
  }
  *d = '\0';
  return dest;
}

char *strdup(const char *s) {
  if (s == NULL) {
    return NULL;
  }

  size_t len = strlen(s);
  char *d = kmalloc(len + 1);
  kassert(d);

  char *p = d;
  while (len--) {
    *p++ = *s++;
  }
  *p = '\0';
  return d;
}

void reverse(char *s) {
  char ch;
  for (int i = 0, j = strlen(s) - 1; i < j; i++, j--) {
    ch = s[i];
    s[i] = s[j];
    s[j] = ch;
  }
}

// utf-16 functions

int utf16_strlen(const char16_t *s) {
  int len = 0;
  while (*s != 0) {
    len++;
    s++;
  }
  return len;
}

// encoding conversion functions

int utf16_iconv_ascii(char *dest, const char16_t *src) {
  int count = 0;
  while (*src != 0) {
    if (*src <= 127) {
      *dest = *src & 0xFF;
      dest++;
      count++;
    }
  }
  return count;
}

int utf16_iconvn_ascii(char *dest, const char16_t *src, size_t n) {
  int count = 0;
  while (n > 0) {
    if (*src <= 127) {
      *dest = *src & 0xFF;
      src++;
      dest++;
      count++;
    }
    n--;
  }
  return count;
}

// string conversion

// adapted from: https://github.com/gcc-mirror/gcc/blob/master/libiberty/strtol.c
long strtol(const char *nptr, char **endptr, int base) {
  const char *s = nptr;
  unsigned long acc;
  int c;
  unsigned long cutoff;
  int neg = 0, any, cutlim;

  /*
   * Skip white space and pick up leading +/- sign if any.
   * If base is 0, allow 0x for hex and 0 for octal, else
   * assume decimal; if base is already 16, allow 0x.
   */
  do {
    c = (int) *s++;
  } while (c == ' ');

  if (c == '-') {
    neg = 1;
    c = (int) *s++;
  } else if (c == '+') {
    c = (int) *s++;
  }

  if ((base == 0 || base == 16) && c == '0' && (*s == 'x' || *s == 'X')) {
    c = (int) s[1];
    s += 2;
    base = 16;
  }

  if (base == 0) {
    base = c == '0' ? 8 : 10;
  }

  /*
   * Compute the cutoff value between legal numbers and illegal
   * numbers.  That is the largest legal value, divided by the
   * base.  An input number that is greater than this value, if
   * followed by a legal input character, is too big.  One that
   * is equal to this value may be valid or not; the limit
   * between valid and invalid numbers is then based on the last
   * digit.  For instance, if the range for longs is
   * [-2147483648..2147483647] and the input base is 10,
   * cutoff will be set to 214748364 and cutlim to either
   * 7 (neg==0) or 8 (neg==1), meaning that if we have accumulated
   * a value > 214748364, or equal but the next digit is > 7 (or 8),
   * the number is too big, and we will return a range error.
   *
   * Set any if any `digits' consumed; make it negative to indicate
   * overflow.
   */
  cutoff = neg ? -(unsigned long)LONG_MIN : LONG_MAX;
  cutlim = cutoff % (unsigned long)base;
  cutoff /= (unsigned long)base;
  for (acc = 0, any = 0; /**/; c = (int) *s++) {
    if (c >= '0' && c <= '9') { // ISDIGIT(c)
      c -= '0';
    } else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) { // ISALPHA(c)
      c -= (c >= 'A' && c <= 'Z') ? 'A' - 10 : 'a' - 10; // ISUPPER(c)
    } else {
      break;
    }

    if (c >= base) {
      break;
    }

    if (any < 0 || acc > cutoff || (acc == cutoff && c > cutlim)) {
      any = -1;
    } else {
      any = 1;
      acc *= base;
      acc += c;
    }
  }

  if (any < 0) {
    acc = neg ? LONG_MIN : LONG_MAX;
  } else if (neg) {
    acc = -acc;
  }

  if (endptr != 0) {
    *endptr = (char *) (any ? s - 1 : nptr);
  }
  return acc;
}


int ltostr_safe(long val, char *buf, size_t len) {
  if (!buf || len < 2) return -1;

  char *p = buf;
  long neg = val < 0;

  if (neg) {
    val = -val;
    *p++ = '-';
    len--;
  }

  // count digits needed
  long temp = val, digits = 0;
  do {
    digits++;
    temp /= 10;
  } while (temp);

  if (digits >= len) return -1;

  p[digits] = '\0';

  // write digits backwards
  do {
    p[--digits] = (char)('0' + (val % 10));
    val /= 10;
  } while (val);

  return 0;
}
