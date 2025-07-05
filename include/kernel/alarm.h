//
// Created by Aaron Gill-Braun on 2025-04-25.
//

#ifndef KERNEL_ALARM_H
#define KERNEL_ALARM_H

#include <kernel/base.h>
#include <kernel/queue.h>
#include <kernel/mutex.h>
#include <kernel/irq.h>
#include <kernel/time.h>

// if undefined the kernel runs in tickless mode
#define TICK_PERIOD  MS_TO_NS(50)

/*
 * Alarm source
 *
 * An alarm source is a hardware device that can generate periodic and/or one-shot
 * interrupt events.
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
  int (*setval)(struct alarm_source *, uint64_t val);

  void *data;
  uint32_t mode;
  int irq_num;

  /* kernel fields */
  mtx_t lock;
  uint64_t last_count;
  LIST_ENTRY(struct alarm_source) list;
} alarm_source_t;

// Alarm source capabilities
#define ALARM_CAP_ONE_SHOT  0x1 // alarm source can generate one-shot events
#define ALARM_CAP_PERIODIC  0x2 // alarm source can generate periodic events
#define ALARM_CAP_ABSOLUTE  0x4 // alarm source is programmed with absolute time values

/*
 * An alarm callback.
 * This contains the function to call and up to 5 arguments to pass to it.
 * The first argument passed to any callback is the alarm itself.
 */
struct callback {
  uintptr_t function;
  void *args[3];
};

typedef struct alarm {
  id_t id;
  uint64_t expires_ns;
  uintptr_t function;
  void *args[3];
  LIST_ENTRY(struct alarm) next;
} alarm_t;

void register_alarm_source(alarm_source_t *as);
void alarm_init();

alarm_source_t *alarm_source_get(const char *name);
alarm_source_t *alarm_tickless_source();
alarm_source_t *alarm_tick_source();
int alarm_source_init(alarm_source_t *as, int mode, irq_handler_t handler);
int alarm_source_enable(alarm_source_t *as);
int alarm_source_disable(alarm_source_t *as);
int alarm_source_setval_abs_ns(alarm_source_t *as, uint64_t abs_ns);
int alarm_source_setval_rel_ns(alarm_source_t *as, uint64_t rel_ns);

#define alarm_cb(fn, ...) ((struct callback){(uintptr_t)(fn), __alarm_cb_args(__VA_ARGS__)})
alarm_t *alarm_alloc_absolute(uint64_t clock_ns, struct callback cb);
alarm_t *alarm_alloc_relative(uint64_t offset_ns, struct callback cb);
void alarm_free(alarm_t **alarmp);

id_t alarm_register(alarm_t *alarm);
int alarm_unregister(id_t alarm_id);

int alarm_sleep_ms(uint64_t ms);

#define __alarm_cb_args(...) __alarm_cb_args_dispatch(__alarm_cb_args_count(__VA_ARGS__), __VA_ARGS__)
#define __alarm_cb_args_nargs(_1, _2, _3, N, ...) N
#define __alarm_cb_args_count(...) __alarm_cb_args_nargs(__VA_ARGS__, 3, 2, 1)
#define __alarm_cb_args_sel_fn(fn, n) fn ## n
#define __alarm_cb_args_dispatch(N, ...) __alarm_cb_args_sel_fn(__alarm_cb_args_, N)(__VA_ARGS__)

#define __alarm_cb_args_1(a) { (void *)(uint64_t)(a), NULL, NULL }
#define __alarm_cb_args_2(a, b) { (void *)(uint64_t)(a), (void *)(uint64_t)(b), NULL }
#define __alarm_cb_args_3(a, b, c) { (void *)(uint64_t)(a), (void *)(uint64_t)(b), (void *)(uint64_t)(c) }



#endif
