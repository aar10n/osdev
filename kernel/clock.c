//
// Created by Aaron Gill-Braun on 2022-06-29.
//

#include <kernel/clock.h>
#include <kernel/time.h>
#include <kernel/proc.h>
#include <kernel/mm.h>

#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/panic.h>
#include <kernel/atomic.h>

#include <kernel/hw/rtc.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(x, ...) kprintf("clock: " x, ##__VA_ARGS__)

static struct tm boot_time_tm;    // time at boot as struct tm
static uint64_t boot_time_epoch;  // boot time in seconds since epoch

static LIST_HEAD(clock_source_t) clock_sources;
static clock_source_t *current_clock_source;
volatile uint64_t current_clock_count;

//
// MARK: Clock Source
//

void register_clock_source(clock_source_t *cs) {
  ASSERT(cs != NULL);
  mtx_assert(&cs->lock, MA_UNLOCKED);

  mtx_init(&cs->lock, MTX_SPIN, "clock_source_lock");
  LIST_ENTRY_INIT(&cs->list);
  LIST_ADD(&clock_sources, cs, list);

  if (!current_clock_source || cs->scale_ns < current_clock_source->scale_ns) {
    current_clock_source = cs;
  }

  DPRINTF("registered clock source '%s'\n", cs->name);
}

static inline clock_source_t *clock_source_find(const char *name) {
  clock_source_t *source;
  LIST_FOREACH(source, &clock_sources, list) {
    if (strcmp(source->name, name) == 0) {
      return source;
    }
  }
  return NULL;
}

////////////////////////////
// MARK: system time/clock


static inline void clock_do_read_sync(clock_source_t *source) {
  uint64_t delta;
  uint64_t count = source->read(source);
  if (count < source->last_count) {
    // clock source has wrapped around
    delta = source->value_mask - source->last_count + count;
  } else {
    delta = count - source->last_count;
  }
  source->last_count = count;
  atomic_fetch_add(&current_clock_count, delta);
}

//

void clock_init() {
  if (current_clock_source == NULL) {
    panic("no clock sources registered");
  }
  kprintf("using %s as clock source\n", current_clock_source->name);

  int res;
  if (current_clock_source->enable(current_clock_source) != 0) {
    panic("failed to enable clock source: %s", current_clock_source->name);
  }
  current_clock_source->last_count = current_clock_source->read(current_clock_source);

  // read boot time from rtc
  struct rtc_time rtc_boot_time;
  rtc_get_time(&rtc_boot_time);
  boot_time_tm.tm_sec = rtc_boot_time.second;
  boot_time_tm.tm_min = rtc_boot_time.minute;
  boot_time_tm.tm_hour = rtc_boot_time.hour;
  boot_time_tm.tm_mday = rtc_boot_time.day;
  boot_time_tm.tm_mon = rtc_boot_time.month;
  boot_time_tm.tm_year = rtc_boot_time.year;
  boot_time_tm.tm_wday = rtc_boot_time.weekday;

  boot_time_epoch = tm2posix(&boot_time_tm);

  curthread->start_time = clock_micro_time();
  curthread->last_sched_ns = clock_get_nanos();
}

//

uint64_t clock_read_sync_nanos() {
  clock_source_t *source = current_clock_source;
  mtx_spin_lock(&source->lock);
  clock_do_read_sync(source);
  mtx_spin_unlock(&source->lock);
  return current_clock_count * source->scale_ns;
}

uint64_t clock_wait_sync_nanos() {
  clock_source_t *source = current_clock_source;

  // use critical enter/exit so we stay in critical section even if lock is contended
  critical_enter();
  if (mtx_spin_trylock(&source->lock)) {
    clock_do_read_sync(source);
    mtx_spin_unlock(&source->lock);
  } else {
    // wait for the other cpu to release lock (meaning time has been updated) and return
    // the just-updated clock count, without wasting time re-reading it from hardware.
    struct spin_delay delay = new_spin_delay(LONG_DELAY, 10000);
    while (mtx_owner(&source->lock) != NULL) {
      if (!spin_delay_wait(&delay)) {
        // possible deadlock?
        panic("spin mutex deadlock %s:%d", __FILE__, __LINE__);
      }
    }
  }

  critical_exit();
  return current_clock_count * source->scale_ns;
}

uint64_t clock_get_uptime() {
  uint64_t uptime = clock_wait_sync_nanos() / NS_PER_SEC;
  return uptime;
}

uint64_t clock_get_starttime() {
  return boot_time_epoch;
}

uint64_t clock_get_millis() {
  return clock_get_nanos() / NS_PER_MS;
}

uint64_t clock_get_micros() {
  return clock_wait_sync_nanos() / NS_PER_USEC;
}

uint64_t clock_get_nanos() {
  return clock_wait_sync_nanos();
}

//
// MARK: System Calls
//

DEFINE_SYSCALL(clock_gettime, int, int clockid, struct timespec *tp) {
  if (!vm_validate_ptr((uintptr_t) tp, /*write=*/true)) {
    return -EFAULT; // invalid user pointer
  }

  uint64_t now_ns = clock_wait_sync_nanos();
  tp->tv_sec = (time_t)((now_ns / NS_PER_SEC) + boot_time_epoch);
  tp->tv_nsec = (time_t)(now_ns % NS_PER_SEC);
  return 0;
}
