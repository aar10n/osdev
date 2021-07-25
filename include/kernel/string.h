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
void reverse(char *s);

int utf16_strlen(const char16_t *s);

int utf16_iconv_ascii(char *dest, const char16_t *src);
int utf16_iconvn_ascii(char *dest, const char16_t *src, size_t n);

#endif
