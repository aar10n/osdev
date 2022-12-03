//
// Created by Aaron Gill-Braun on 2020-10-20.
//

#ifndef KERNEL_TIMER_H
#define KERNEL_TIMER_H

#include <base.h>
#include <queue.h>
#include <spinlock.h>

// Indicates the timer is not shared between logical CPUs.
#define TIMER_CAP_PER_CPU 0x1

typedef enum timer_mode {
  TIMER_ONE_SHOT = 0x1,
  TIMER_PERIODIC = 0x2,
} timer_mode_t;

typedef struct timer_device {
  const char *name;
  void *data;

  uint8_t irq;
  uint16_t flags;
  timer_mode_t modes;
  uint32_t scale_ns;
  uint64_t value_mask;
  spinlock_t lock;

  // timer api
  int (*init)(struct timer_device *, timer_mode_t mode);
  int (*enable)(struct timer_device *);
  int (*disable)(struct timer_device *);
  int (*setval)(struct timer_device *, uint64_t ns);

  // set by the timer subsystem
  void (*irq_handler)(struct timer_device *);

  LIST_ENTRY(struct timer_device) list;
} timer_device_t;

typedef void (*timer_cb_t)(void *);


void register_timer_device(timer_device_t *device);

int init_periodic_timer();
int init_oneshot_timer();

void alarms_init();
void alarm_reschedule();
clockid_t timer_create_alarm(clock_t expires, timer_cb_t callback, void *data);
void *timer_delete_alarm(clockid_t id);
clock_t timer_now();

int timer_enable(uint16_t type);
int timer_disable(uint16_t type);
int timer_setval(uint16_t type, clock_t value);

void timer_udelay(uint64_t us);

void timer_dump_pending_alarms();

#endif
