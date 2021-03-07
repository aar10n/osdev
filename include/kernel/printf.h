//
// Created by Aaron Gill-Braun on 2021-03-07.
//

#ifndef KERNEL_PRINTF_H
#define KERNEL_PRINTF_H

#include <base.h>
#include <stdarg.h>

void kprintf(const char *format, ...);
void kvfprintf(const char *format, va_list args);

int ksprintf(char *str, const char *format, ...);
int kvsprintf(char *str, const char *format, va_list args);

int ksnprintf(char *str, size_t n, const char *format, ...);
int kvsnprintf(char *str, size_t n, const char *format, va_list args);

#endif
