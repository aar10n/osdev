//
// Created by Aaron Gill-Braun on 2022-06-29.
//

#ifndef KERNEL_CLOCK_H
#define KERNEL_CLOCK_H

#include <kernel/base.h>
#include <kernel/queue.h>
#include <kernel/mutex.h>
#include <kernel/irq.h>

#define HZ 100 // system clock frequency
#define SCALE_TICKS(t, s) ((t) * ((s) / HZ))

/*
 * Clock source
 *
 * A clock source is a hardware device that provides a time source for the kernel.
 */
typedef struct clock_source {
  /* driver fields */
  const char *name;
  uint32_t scale_ns;
  uint64_t value_mask;

  int (*enable)(struct clock_source *);
  int (*disable)(struct clock_source *);
  uint64_t (*read)(struct clock_source *);

  void *data;

  /* kernel fields */
  mtx_t lock;
  uint64_t last_count;
  LIST_ENTRY(struct clock_source) list;
} clock_source_t;

/*
 * Alarm source
 *
 * An alarm source is a hardware device that can generate interrupts after a set
 * amount of time has passed.
 */
typedef struct alarm_source {
  /* driver fields */
  const char *name;
  uint32_t cap_flags;
  uint32_t scale_ns;
  uint64_t value_mask;

  int (*init)(struct alarm_source *, uint32_t mode, irq_handler_t);
  int (*enable)(struct alarm_source *);
  int (*disable)(struct alarm_source *);
  int (*setval)(struct alarm_source *, uint64_t value);

  void *data;
  uint32_t mode;
  int irq_num;

  /* kernel fields */
  mtx_t lock;
  uint64_t last_count;
  LIST_ENTRY(struct alarm_source) list;
} alarm_source_t;

#define ALARM_PER_CPU   0x1
#define ALARM_ONE_SHOT  0x2
#define ALARM_PERIODIC  0x4

void register_alarm_source(alarm_source_t *as);
void register_clock_source(clock_source_t *cs);

void clock_init();

/// Reads the current time from the clock source, updates the reference count
/// and then returns the reported clock time in nanoseconds. This function is
/// slow but produces the highest precision timestamp possible.
uint64_t clock_read_sync_nanos();

/// Does the same as `clock_read_sync_nanos` but is better for multi-cpu as it
/// waits for clock updates from other cpus instead of always re-reading it.
uint64_t clock_wait_sync_nanos();

/// Does the same as `clock_read_sync_nanos` only if the current clock lock can be
/// acquired immediately. If the lock cannot be acquired, the function returns
/// the approximate time in nanoseconds.
uint64_t clock_try_sync_nanos();

/// Returns the number of kernel clock ticks.
uint64_t clock_get_ticks();
/// Returns the number of seconds since boot.
uint64_t clock_get_uptime();

/// Returns the time of kernel start as a POSIX time in seconds.
uint64_t clock_get_starttime();

/* clock_get_[millis|micros|nanos]
 *
 * These functions return the kernel time (since boot) at the specified precision.
 * Unless high degree of precision is required (eg. scheduling), it is recommended
 * to use `clock_get_millis`.
 */
uint64_t clock_get_millis();
uint64_t clock_get_micros();
uint64_t clock_get_nanos();

/* clock_[micro|nano]_time
 *
 * These functions return a time value representing the current system utc posix time
 * at the specified precision.
 */
static inline struct timeval clock_micro_time() {
  uint64_t uptime = clock_get_uptime();
  uint64_t micros = clock_get_micros();

  uint64_t tvsec = clock_get_starttime() + uptime;
  uint64_t tvusec = micros - (uptime * US_PER_SEC);
  return (struct timeval){(time_t)tvsec, (time_t)tvusec};
}
static inline struct timespec clock_nano_time() {
  uint64_t uptime = clock_get_uptime();
  uint64_t nanos = clock_get_nanos();

  uint64_t tvsec = clock_get_starttime() + uptime;
  uint64_t tvnsec = nanos - (uptime * NS_PER_SEC);
  return (struct timespec){(time_t)tvsec, (time_t)tvnsec};
}


#endif
