//
// Created by Aaron Gill-Braun on 2022-06-29.
//

#include <kernel/clock.h>

#include <kernel/cpu/cpu.h>
#include <kernel/cpu/io.h>

#include <kernel/printf.h>
#include <kernel/panic.h>
#include <atomic.h>


LIST_HEAD(clock_source_t) clock_sources;
clock_source_t *current_clock_source;
clock_t kernel_time_ns;
uint64_t clock_ticks;

static uint8_t _smp_lock = 0;
static volatile uint8_t *smp_lock = &_smp_lock;

void register_clock_source(clock_source_t *source) {
  kassert(source != NULL);
  spin_init(&source->lock);
  LIST_ENTRY_INIT(&source->list);
  LIST_ADD(&clock_sources, source, list);

  if (!current_clock_source || source->scale_ns < current_clock_source->scale_ns) {
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

clock_t clock_now() {
  clock_source_t *source = current_clock_source;
  if (source == NULL) {
    return 0;
  }

  uint64_t flags;
  temp_irq_save(flags);
  if (atomic_lock_test_and_set(smp_lock) == 0) {
    // we have the lock, now lets update the time
    uint64_t last = source->last_tick;
    uint64_t current = source->read(source);
    source->last_tick = current;
    clock_ticks += current - last;
    kernel_time_ns += (clock_t)(current - last) * source->scale_ns;
    atomic_lock_test_and_reset(smp_lock);
  } else {
    // wait for the time to be updated by another cpu
    register uint64_t timeout asm ("r15") = 10000000 + (PERCPU_ID * 100000);
    while (*smp_lock) {
      cpu_pause();
      timeout--;
      if (timeout == 0) {
        panic("stuck waiting for clock smp_lock");
      }
    }
  }

  temp_irq_restore(flags);
  return kernel_time_ns;
}

clock_t clock_kernel_time_ns() {
  return kernel_time_ns;
}

uint64_t clock_current_ticks() {
  return clock_ticks;
}

void clock_update_ticks() {
  if (current_clock_source == NULL) {
    return;
  }

  clock_source_t *source = current_clock_source;
  spin_lock(&source->lock);

  uint64_t last = source->last_tick;
  uint64_t current = source->read(source);
  source->last_tick = current;
  uint64_t delta = current - last;
  atomic_fetch_add(&clock_ticks, delta);
  atomic_fetch_add(&kernel_time_ns, delta * source->scale_ns);

  spin_unlock(&source->lock);
}

MODULE_INIT(clock_init);
