//
// Created by Aaron Gill-Braun on 2021-07-25.
//

#ifndef KERNEL_STRING_H
#define KERNEL_STRING_H

#include <base.h>

int memcmp(const void *str1, const void *str2, size_t count);
void *memcpy(void *dest, const void *src, size_t len);
void *memmove(void *dest, const void *src, size_t len);
void *memset(void *dest, int val, size_t len);

int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
int strlen(const char *s);
void *strcpy(char *dest, const char *src);
char *strdup(const char *s);
void reverse(char *s);

int utf16_strlen(const char16_t *s);

int utf16_iconv_ascii(char *dest, const char16_t *src);
int utf16_iconvn_ascii(char *dest, const char16_t *src, size_t n);

long strtol(const char *nptr, char **endptr, int base);

void *__memset_slow(void *dest, int val, size_t len);
void *__memset8(void *dest, uint8_t val, size_t len);
void *__memset32(void *dest, uint32_t val, size_t len);
void *__memset64(void *dest, uint64_t val, size_t len);

#endif
