//
// Created by Aaron Gill-Braun on 2022-06-05.
//

#ifndef KERNEL_CONSOLE_H
#define KERNEL_CONSOLE_H

#include <base.h>

typedef struct console {
  void *ptr;
  void (*puts)(void *ptr, const char *s);
  void (*putc)(void *ptr, char c);
  char (*getc)(void *ptr);
} console_t;

void kputs(const char *s);
void kputc(char c);
char kgetc(void);

void console_early_init();

#endif
