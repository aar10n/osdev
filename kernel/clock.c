//
// Created by Aaron Gill-Braun on 2022-06-29.
//

#include <clock.h>

#include <printf.h>
#include <panic.h>
#include <atomic.h>

LIST_HEAD(clock_source_t) clock_sources;
clock_source_t *current_clock_source;
uint64_t kernel_elapsed_time_us;
uint64_t clock_ticks;

void register_clock_source(clock_source_t *source) {
  kassert(source != NULL);
  LIST_ENTRY_INIT(&source->list);
  LIST_ADD(&clock_sources, source, list);

  if (current_clock_source == NULL) {
    current_clock_source = source;
  } else if (source->scale_ns < current_clock_source->scale_ns) {
    current_clock_source = source;
  }

  kprintf("clock: registering clock source '%s'\n", source->name);
}

//

void clock_init() {
  if (current_clock_source == NULL) {
    panic("no clock sources registered");
  }

  kprintf("using %s as clock source\n", current_clock_source->name);
  current_clock_source->enable(current_clock_source);
  current_clock_source->last_tick = current_clock_source->read(current_clock_source);
}

uint64_t clock_now() {
  if (current_clock_source == NULL) {
    return 0;
  }

  uint64_t now_ticks = current_clock_source->read(current_clock_source);
  current_clock_source->last_tick = now_ticks;
  return now_ticks;
}

uint64_t clock_now_ns() {
  if (current_clock_source == NULL) {
    return 0;
  }

  uint64_t now_ticks = current_clock_source->read(current_clock_source);
  current_clock_source->last_tick = now_ticks;
  return now_ticks * current_clock_source->scale_ns;
}

uint32_t clock_period_ns() {
  if (current_clock_source == NULL) {
    return 0;
  }
  return current_clock_source->scale_ns;
}

uint64_t clock_delta_ns() {
  if (current_clock_source == NULL) {
    return 0;
  }

  uint64_t last = current_clock_source->last_tick;
  uint64_t current = current_clock_source->read(current_clock_source);
  current_clock_source->last_tick = current;
  return (current - last) * current_clock_source->scale_ns;
}

void clock_update_ticks() {
  if (current_clock_source == NULL) {
    return;
  }

  uint64_t last = current_clock_source->last_tick;
  uint64_t current = current_clock_source->read(current_clock_source);
  current_clock_source->last_tick = current;
  uint64_t delta = (kernel_elapsed_time_us) * current_clock_source->scale_ns;
  atomic_fetch_add(&clock_ticks, current - last);
}