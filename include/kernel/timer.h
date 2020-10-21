//
// Created by Aaron Gill-Braun on 2020-10-20.
//

#ifndef KERNEL_TIMER_H
#define KERNEL_TIMER_H

#include <base.h>

typedef void (*timer_cb_t)(void *);

typedef struct timer {
  uint64_t id;
  clock_t expiry;
  timer_cb_t callback;
  void *data;
} timer_t;

uint64_t timer_now();

void timer_init();
void create_timer(clock_t ns, timer_cb_t callback, void *data);
void timer_print_debug();

#endif
