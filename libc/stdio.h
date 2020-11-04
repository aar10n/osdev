//
// Created by Aaron Gill-Braun on 2019-04-21.
//

#ifndef LIBC_STDIO_H
#define LIBC_STDIO_H

#include <base.h>
#include <stdarg.h>
#include <stddef.h>
#include <errno.h>

void kprintf(const char *format, ...);
void kvfprintf(const char *format, va_list args);

int ksprintf(char *str, const char *format, ...);
int kvsprintf(char *str, const char *format, va_list args);

int ksnprintf(char *str, size_t n, const char *format, ...);
int kvsnprintf(char *str, size_t n, const char *format, va_list args);

void stdio_lock();
void stdio_unlock();

#endif // LIBC_STDIO_H
