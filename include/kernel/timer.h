//
// Created by Aaron Gill-Braun on 2020-10-20.
//

#ifndef KERNEL_TIMER_H
#define KERNEL_TIMER_H

#include <base.h>
#include <queue.h>
#include <spinlock.h>

#define TIMER_ONE_SHOT 0x1
#define TIMER_PERIODIC 0x2
#define TIMER_CAP_PER_CPU  0x4

typedef struct timer_device {
  const char *name;
  void *data;

  uint8_t irq;
  uint16_t flags;

  // timer api
  int (*init)(struct timer_device *, uint16_t mode);
  int (*enable)(struct timer_device *);
  int (*disable)(struct timer_device *);
  int (*setval)(struct timer_device *, uint64_t ns);

  // set by the timer subsystem
  void (*irq_handler)(struct timer_device *);

  LIST_ENTRY(struct timer_device) list;
} timer_device_t;

typedef void (*timer_cb_t)(void *);
typedef struct timer {
  id_t id;
  uint8_t cpu;
  clock_t expiry;
  timer_cb_t callback;
  void *data;
} timer_event_t;


void register_timer_device(timer_device_t *device);

int init_periodic_timer();
int init_oneshot_timer();

int timer_enable(uint16_t type);
int timer_disable(uint16_t type);
int timer_setval(uint16_t type, uint64_t value);

uint64_t timer_now();

id_t create_timer(clock_t ns, timer_cb_t callback, void *data);
void *timer_cancel(id_t id);
void timer_print_debug();

#endif
