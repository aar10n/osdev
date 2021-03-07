//
// Created by Aaron Gill-Braun on 2021-03-07.
//

#ifndef LIB_FORMAT_H
#define LIB_FORMAT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

int print_format(const char *format, char *str, size_t size, va_list args, bool limit);

#endif
