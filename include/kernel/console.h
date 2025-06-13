//
// Created by Aaron Gill-Braun on 2025-06-01.
//

#ifndef KERNEL_CONSOLE_H
#define KERNEL_CONSOLE_H

#include <kernel/base.h>
#include <kernel/input.h>
#include <kernel/kio.h>
#include <kernel/str.h>

struct tty;

typedef struct console {
  const char *name;
  struct tty *tty;          // associated tty device
  LIST_ENTRY(struct console) list;
} console_t;


void console_register(console_t *console);
void console_init();

void console_key_input(input_key_event_t key);
void console_write_kio(kio_t *kio);
void console_write_str(str_t str);
void console_write_cstr(cstr_t str);

#endif
