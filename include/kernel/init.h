//
// Created by Aaron Gill-Braun on 2022-06-21.
//

#ifndef KERNEL_INIT_H
#define KERNEL_INIT_H

#include <kernel/base.h>

typedef void (*init_callback_t)(void *);

void register_init_address_space_callback(init_callback_t callback, void *data);
void execute_init_address_space_callbacks();

void do_static_initializers();
void do_module_initializers();

#endif
