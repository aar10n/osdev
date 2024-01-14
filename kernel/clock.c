//
// Created by Aaron Gill-Braun on 2022-06-29.
//

#include <kernel/clock.h>
#include <kernel/time.h>
#include <kernel/proc.h>

#include <kernel/string.h>
#include <kernel/printf.h>
#include <kernel/panic.h>
#include <kernel/atomic.h>

#include <kernel/hw/rtc.h>

#define ASSERT(x) kassert(x)

volatile uint64_t tick_count;     // system tick count
volatile uint64_t uptime_seconds; // system uptime in seconds
static struct tm boot_time_tm;    // time at boot as struct tm
static uint64_t boot_time_epoch;  // boot time in seconds since epoch

static LIST_HEAD(alarm_source_t) alarm_sources;
static LIST_HEAD(clock_source_t) clock_sources;
static clock_source_t *current_clock_source;
volatile uint64_t current_clock_count;

// MARK: alarm and clock sources

void register_alarm_source(alarm_source_t *as) {
  ASSERT(as != NULL);

  mtx_init(&as->lock, MTX_SPIN, "alarm_source_lock");
  if ((as->cap_flags & ALARM_ONE_SHOT) == 0 && (as->cap_flags & ALARM_PERIODIC) == 0) {
    panic("alarm source '%s' must support either one-shot or periodic mode", as->name);
  }

  LIST_ENTRY_INIT(&as->list);
  LIST_ADD(&alarm_sources, as, list);

  kprintf("timer: registering alarm source '%s'\n", as->name);
}

void register_clock_source(clock_source_t *cs) {
  ASSERT(cs != NULL);
  mtx_assert(&cs->lock, MA_UNLOCKED);

  mtx_init(&cs->lock, MTX_SPIN, "clock_source_lock");
  LIST_ENTRY_INIT(&cs->list);
  LIST_ADD(&clock_sources, cs, list);

  if (!current_clock_source || cs->scale_ns < current_clock_source->scale_ns) {
    current_clock_source = cs;
  }

  kprintf("clock: registering clock source '%s'\n", cs->name);
}

//

static inline clock_source_t *clock_source_find(const char *name) {
  clock_source_t *source;
  LIST_FOREACH(source, &clock_sources, list) {
    if (strcmp(source->name, name) == 0) {
      return source;
    }
  }
  return NULL;
}

static inline alarm_source_t *alarm_source_find(const char *name) {
  alarm_source_t *source;
  LIST_FOREACH(source, &alarm_sources, list) {
    if (strcmp(source->name, name) == 0) {
      return source;
    }
  }
  return NULL;
}

static inline alarm_source_t *alarm_source_find_by_cap(uint32_t cap) {
  alarm_source_t *best = NULL;
  LIST_FOR_IN(as, &alarm_sources, list) {
    if ((as->cap_flags & cap) == cap) {
      if (best == NULL || as->scale_ns < best->scale_ns) {
        best = as;
      }
    }
  }
  return NULL;
}

////////////////////////////
// MARK: system time/clock

// handler is called HZ times per second by a periodic alarm source
static void clocktick_irq_handler(struct trapframe *frame) {
  atomic_fetch_add(&tick_count, 1);
  if (tick_count % HZ == 0) {
    atomic_fetch_add(&uptime_seconds, 1);
  }
}

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
  current_clock_source->enable(current_clock_source);
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

  // setup the interrupt that provides us our clock tick
  alarm_source_t *tick_as = alarm_source_find("pit");
  if (tick_as == NULL) {
    panic("no alarm source found");
  }

  int res;
  if ((res = tick_as->init(tick_as, ALARM_PERIODIC, clocktick_irq_handler)) < 0) {
    panic("failed to enable clock alarm source: %s", tick_as->name);
  }
  // tick HZ times per second
  uint64_t val = ((NS_PER_SEC / HZ) * tick_as->scale_ns) & tick_as->value_mask;
  if ((res = tick_as->setval(tick_as, val)) < 0) {
    panic("failed to set clock alarm source value: %s [val=%ull]", tick_as->name, val);
  }
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
    // the just-updated clock count, without wasting re-reading it from hardware
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

uint64_t clock_try_sync_nanos() {
  clock_source_t *source = current_clock_source;
  if (mtx_spin_trylock(&source->lock)) {
    clock_do_read_sync(source);
    mtx_spin_unlock(&source->lock);
    return current_clock_count * source->scale_ns;
  } else {
    // time is being read and updated by another cpu. first check to see if
    // the current_clock_count is more recent than the kernel clock time, otherwise
    // settle for the millisecond level precision
    uint64_t curr_ns = SCALE_TICKS(tick_count, MS_PER_SEC);
    uint64_t clock_ns = current_clock_count * source->scale_ns;
    return max(clock_ns, curr_ns);
  }
}

uint64_t clock_get_ticks() {
  return tick_count;
}

uint64_t clock_get_uptime() {
  return uptime_seconds;
}

uint64_t clock_get_starttime() {
  return boot_time_epoch;
}

uint64_t clock_get_millis() {
  return SCALE_TICKS(tick_count, MS_PER_SEC);
}

uint64_t clock_get_micros() {
  return clock_wait_sync_nanos() / NS_PER_USEC;
}

uint64_t clock_get_nanos() {
  // return clock_read_sync_nanos();
  return clock_wait_sync_nanos();
}
