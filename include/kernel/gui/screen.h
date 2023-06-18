//
// Created by Aaron Gill-Braun on 2021-04-20.
//

#ifndef KERNEL_GUI_SCREEN_H
#define KERNEL_GUI_SCREEN_H

#include <kernel/base.h>

void screen_early_init();

void screen_print_char(char ch);
void screen_print_str(const char *string);

#endif
