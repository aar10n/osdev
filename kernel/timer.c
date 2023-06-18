//
// Created by Aaron Gill-Braun on 2020-10-20.
//

#include <kernel/timer.h>

#include <kernel/mm.h>
#include <kernel/clock.h>
#include <kernel/thread.h>
#include <kernel/mutex.h>
#include <kernel/irq.h>
#include <kernel/spinlock.h>
#include <kernel/printf.h>
#include <kernel/panic.h>

#include <atomic.h>
#include <rb_tree.h>


typedef struct timer_alarm {
  clockid_t id;
  clock_t expires;
  timer_cb_t callback;
  void *data;
} timer_alarm_t;

// void scheduler_tick();

static rb_tree_t *pending_alarm_tree;
static rb_tree_t *alarm_expiry_tree;
static spinlock_t pending_alarm_lock;
static cond_t alarm_cond;
static spinlock_t alarm_cond_lock;
static uint32_t next_alarm_id;

timer_device_t *global_periodic_timer;
timer_device_t *global_one_shot_timer;

LIST_HEAD(timer_device_t) timer_devices;


void timer_periodic_handler(timer_device_t *td) {
  kprintf("---> tick <---\n");
  // clock_update_ticks();
  // panic("sched_tick not implemented");
}

void timer_oneshot_handler(timer_device_t *td) {
  if (spin_trylock(&alarm_cond_lock)) {
    // clock_update_ticks();
    cond_signal(&alarm_cond);
    spin_unlock(&alarm_cond_lock);
  }
}

int set_alarm_timer_value(timer_device_t *timer, clock_t expiry) {
  uint64_t timer_value = expiry / timer->scale_ns;
  if (timer_value > timer->value_mask) {
    return -EOVERFLOW;
  }
  return timer->setval(timer, timer_value);
}

noreturn void *alarm_event_loop(unused void *arg) {
  kassert(global_one_shot_timer != NULL);
  timer_device_t *timer = global_one_shot_timer;
  thread_setaffinity(cpu_bsp_id); // pin to CPU#0

  kprintf("timer: starting alarm event loop\n");
  while (true) {
    cond_wait(&alarm_cond);
    if (alarm_expiry_tree->min == NULL) {
      continue;
    }

  LABEL(dispatch);
    if (alarm_expiry_tree->nodes == 0) {
      continue;
    }

    clock_t now = timer_now();
    timer_alarm_t *alarm = alarm_expiry_tree->min->data;
    kassert(alarm);
    if (alarm->expires > now) {
      kassert(timer != NULL);
      if (set_alarm_timer_value(timer, alarm->expires) < 0) {
        panic("failed to set alarm timer value");
      }
      continue;
    }

    rb_tree_delete_node(alarm_expiry_tree, alarm_expiry_tree->min);
    rb_tree_delete(pending_alarm_tree, alarm->expires);

    preempt_enable();
    alarm->callback(alarm->data);
    preempt_disable();

    kfree(alarm);
    if (pending_alarm_tree->nodes > 0) {
      goto dispatch;
    }
  }
}

//

void register_timer_device(timer_device_t *device) {
  kassert(device != NULL);
  spin_init(&device->lock);
  if ((device->modes & TIMER_ONE_SHOT) == 0 && (device->modes & TIMER_PERIODIC) == 0) {
    panic("timer device '%s' must support either one-shot or periodic mode", device->name);
  }

  LIST_ENTRY_INIT(&device->list);
  LIST_ADD(&timer_devices, device, list);

  kprintf("timer: registering timer device '%s'\n", device->name);
}

int init_periodic_timer() {
  if (global_periodic_timer != NULL) {
    // only re-initialize if timer is per-cpu
    if (global_periodic_timer->flags & TIMER_CAP_PER_CPU) {
      return global_periodic_timer->init(global_periodic_timer, TIMER_PERIODIC);
    }
    return 0;
  }

  timer_device_t *device = NULL;
  LIST_FOREACH(device, &timer_devices, list) {
    if (device == global_one_shot_timer || !(device->modes & TIMER_PERIODIC)) {
      continue;
    }

    global_periodic_timer = device;
    break;
  }

  if (global_periodic_timer == NULL) {
    return -ENODEV;
  }

  global_periodic_timer->irq_handler = timer_periodic_handler;
  return global_periodic_timer->init(global_periodic_timer, TIMER_PERIODIC);
}

int init_oneshot_timer() {
  if (global_one_shot_timer != NULL) {
    // a one-shot timer has already been selected
    if (global_one_shot_timer->flags & TIMER_CAP_PER_CPU) {
      // only re-initialize if timer is per-cpu
      return global_one_shot_timer->init(global_one_shot_timer, TIMER_ONE_SHOT);
    }
    return 0;
  }

  timer_device_t *device = NULL;
  LIST_FOREACH(device, &timer_devices, list) {
    if (device == global_periodic_timer || !(device->modes & TIMER_ONE_SHOT)) {
      continue;
    }

    global_one_shot_timer = device;
    break;
  }

  if (global_one_shot_timer == NULL) {
    return -ENODEV;
  }

  global_one_shot_timer->irq_handler = timer_oneshot_handler;
  return global_one_shot_timer->init(global_one_shot_timer, TIMER_ONE_SHOT);
}

//

void alarms_init() {
  kassert(global_one_shot_timer != NULL);
  spin_init(&pending_alarm_lock);
  pending_alarm_tree = create_rb_tree();
  alarm_expiry_tree = create_rb_tree();

  cond_init(&alarm_cond, 0);
  spin_init(&alarm_cond_lock);
  thread_create(alarm_event_loop, NULL);
}

void alarm_reschedule() {
  cond_signal(&alarm_cond);
}

clockid_t timer_create_alarm(clock_t expires, timer_cb_t callback, void *data) {
  kassert(global_one_shot_timer != NULL);
  timer_device_t *timer = global_one_shot_timer;

  if (expires < clock_now()) {
    kprintf("timer: invalid alarm %llu < %llu\n", expires, clock_now());
    return -EINVAL;
  }

  uint32_t id = atomic_fetch_add(&next_alarm_id, 1);
  timer_alarm_t *alarm = kmalloc(sizeof(timer_alarm_t));
  alarm->id = id;
  alarm->expires = expires;
  alarm->callback = callback;
  alarm->data = data;

  spin_lock(&pending_alarm_lock);
  rb_tree_insert(pending_alarm_tree, id, alarm);
  rb_tree_insert(alarm_expiry_tree, expires, alarm);

  // check if timer needs to be updated
  if (alarm == alarm_expiry_tree->min->data) {
    // set timer to next most-recently expiring alarm deadline
    if (set_alarm_timer_value(timer, alarm->expires) < 0) {
      panic("failed to set alarm timer value");
    }
  }
  spin_unlock(&pending_alarm_lock);

  clock_t margin = clock_now() + global_one_shot_timer->scale_ns;
  if (expires < margin) {
    // if we pass the expiry at this point its possible that we were
    // too late in programming the underlying timer and missed the
    // deadline. we signal manually here to ensure we dont get stuck
    cond_signal(&alarm_cond);
  }
  return alarm->id;
}

void *timer_delete_alarm(clockid_t id) {
  spin_lock(&pending_alarm_lock);
  timer_alarm_t *alarm = rb_tree_delete(pending_alarm_tree, id);
  rb_tree_delete(alarm_expiry_tree, alarm->expires);

  spin_unlock(&pending_alarm_lock);
  if (alarm == NULL) {
    return NULL;
  }

  void *data = alarm->data;
  kfree(alarm);
  return data;
}

clock_t timer_now() {
  return clock_now();
}

//

int timer_enable(uint16_t type) {
  timer_device_t *td = NULL;
  if (type == TIMER_PERIODIC) {
    td = global_periodic_timer;
  } else if (type == TIMER_ONE_SHOT) {
    td = global_one_shot_timer;
  } else {
    return -EINVAL;
  }

  if (td == NULL) {
    return -ENODEV;
  }

  return td->enable(td);
}

int timer_disable(uint16_t type) {
  timer_device_t *td = NULL;
  if (type == TIMER_PERIODIC) {
    td = global_periodic_timer;
  } else if (type == TIMER_ONE_SHOT) {
    td = global_one_shot_timer;
  } else {
    return -EINVAL;
  }

  if (td == NULL) {
    return -ENODEV;
  }

  return td->disable(td);
}

int timer_setval(uint16_t type, clock_t value) {
  timer_device_t *td = NULL;
  if (type == TIMER_PERIODIC) {
    td = global_periodic_timer;
  } else if (type == TIMER_ONE_SHOT) {
    td = global_one_shot_timer;
  } else {
    return -EINVAL;
  }

  if (td == NULL) {
    return -ENODEV;
  }

  uint64_t count = value / td->scale_ns;
  if (count > td->value_mask) {
    return -EOVERFLOW;
  }

  kprintf("timer: setval() value=%llu count=%llu\n", value, count);
  return td->setval(td, count);
}

//

void timer_udelay(uint64_t us) {
  clock_t deadline = clock_future_time(US_TO_NS(us));
  while (clock_now() < deadline) {
    cpu_pause();
    cpu_pause();
  }
}

//

void timer_dump_pending_alarms() {
  kprintf("  now = %llu\n", clock_now());
  if (alarm_expiry_tree == NULL) {
    return;
  }

  rb_node_t *min = alarm_expiry_tree->min;
  rb_node_t *max = alarm_expiry_tree->max;
  if (min) {
    kprintf("  min key = %llu\n", min->key);
  }
  if (max) {
    kprintf("  max key = %llu\n", max->key);
  }

  rb_iter_t iter = {};
  rb_tree_init_iter(alarm_expiry_tree, min, FORWARD, &iter);

  kprintf("   ");
  rb_node_t *node;
  while ((node = rb_iter_next(&iter))) {
    timer_alarm_t *alarm = node->data;
    kprintf(" -> %llu [%llu]", alarm->id, alarm->expires);
  }
  kprintf("\n");
}
