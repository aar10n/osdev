//
// Created by Aaron Gill-Braun on 2022-06-29.
//

#ifndef KERNEL_CLOCK_H
#define KERNEL_CLOCK_H

#include <base.h>
#include <queue.h>

typedef struct clock_source {
  const char *name;
  void *data;

  uint32_t scale_ns;
  uint64_t last_tick;

  int (*enable)(struct clock_source *);
  int (*disable)(struct clock_source *);
  uint64_t (*read)(struct clock_source *);

  LIST_ENTRY(struct clock_source) list;
} clock_source_t;

void register_clock_source(clock_source_t *source);

void clock_init();
uint64_t clock_now();
uint64_t clock_now_ns();
uint32_t clock_period_ns();
uint64_t clock_delta_ns();

void clock_update_ticks();

#endif