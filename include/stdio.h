//
// Created by Aaron Gill-Braun on 2019-04-21.
//

#include <stddef.h>
#ifndef LIBC_STDIO_H
#define LIBC_STDIO_H

int ksnprintf(char *str, size_t n, const char *format, ...);
int ksprintf(char *str, const char *format, ...);
void kprintf(const char *format, ...);

#endif // LIBC_STDIO_H
