//
// Created by Aaron Gill-Braun on 2022-06-05.
//

#ifndef KERNEL_CONSOLE_H
#define KERNEL_CONSOLE_H

#include <base.h>

/// Writes a formatted string to the kernel command line.
void kputsf(const char *format, ...);
/// Writes a string to the kernel command line.
void kputs(const char *s);
/// Writes a single character to the kernel command line.
void kputc(char c);
/// Gets the next character from the kernel command line input.
int kgetc(void);

/// Writes a string to the kernel debug console.
void debug_kputs(const char *s);
/// Writes a single character to the kernel debug console.
void debug_kputc(char c);

void console_early_init();

#endif
