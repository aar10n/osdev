//
// Created by Aaron Gill-Braun on 2019-04-21.
//

#ifndef LIBC_STDIO_H
#define LIBC_STDIO_H

#include <stdarg.h>
#include <stddef.h>

int ksnprintf(char *str, size_t n, const char *format, ...);
int kvsnprintf(char *str, size_t n, const char *format, va_list args);

int ksprintf(char *str, const char *format, ...);
int kvsprintf(char *str, const char *format, va_list args);

void kprintf(const char *format, ...);
void kvfprintf(const char *format, va_list args);

#endif // LIBC_STDIO_H
