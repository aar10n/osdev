//
// Created by Aaron Gill-Braun on 2021-07-25.
//

#include <string.h>

extern const char *errno_str[];

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

int strncmp(const char *s1, const char *s2, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (*s1 != *s2) break;
    s1++;
    s2++;
  }

  return *s1 - *s2;
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

