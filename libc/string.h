//
// Created by Aaron Gill-Braun on 2019-04-21.
//

#ifndef LIBC_STRING_H
#define LIBC_STRING_H

#include <stddef.h>
#include <stdint.h>
#include <errno.h>

int memcmp(const void *str1, const void *str2, size_t count);
void *memcpy(void *dest, const void *src, size_t len);
void *memmove(void *dest, const void *src, size_t len);
void *memset(void *dest, int val, size_t len);

int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
int strlen(const char *s);
void *strcpy(char *dest, const char *src);
void reverse(char *s);

#endif // LIBC_STRING_H