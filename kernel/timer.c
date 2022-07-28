//
// Created by Aaron Gill-Braun on 2020-10-20.
//

#include <timer.h>

#include <mm.h>
#include <clock.h>
#include <thread.h>
#include <mutex.h>
#include <irq.h>
#include <scheduler.h>
#include <spinlock.h>
#include <printf.h>
#include <panic.h>

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
static spinlock_t pending_alarm_lock;
static cond_t alarm_cond;
static spinlock_t alarm_cond_lock;
static uint32_t next_alarm_id;

timer_device_t *global_periodic_timer;
timer_device_t *global_one_shot_timer;

LIST_HEAD(timer_device_t) timer_devices;


void timer_periodic_handler(timer_device_t *td) {
  kprintf("---> tick <---\n");
  clock_update_ticks();
  panic("sched_tick not implemented");
}

void timer_oneshot_handler(timer_device_t *td) {
  if (spin_trylock(&alarm_cond_lock)) {
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

noreturn void *alarm_event_loop(void *arg) {
  kprintf("timer: starting alarm event loop\n");
  timer_device_t *timer = arg;
  while (true) {
    cond_wait(&alarm_cond);
    if (pending_alarm_tree->min == NULL) {
      continue;
    }

  LABEL(dispatch);
    spin_lock(&pending_alarm_lock);
    if (pending_alarm_tree->nodes == 0) {
      spin_unlock(&pending_alarm_lock);
      continue;
    }

    timer_alarm_t *alarm = pending_alarm_tree->min->data;
    kassert(alarm);
    if (alarm->expires > timer_now()) {
      set_alarm_timer_value(timer, alarm->expires);
      spin_unlock(&pending_alarm_lock);
      continue;
    }

    rb_tree_delete_node(pending_alarm_tree, pending_alarm_tree->min);
    spin_unlock(&pending_alarm_lock);

    PERCPU_THREAD->preempt_count++;
    alarm->callback(alarm->data);
    PERCPU_THREAD->preempt_count--;

    kfree(alarm);
    if (pending_alarm_tree->nodes > 0) {
      goto dispatch;
    }
  }
}

//

void register_timer_device(timer_device_t *device) {
  kassert(device != NULL);
  if ((device->flags & TIMER_ONE_SHOT) == 0 && (device->flags & TIMER_PERIODIC) == 0) {
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
    if (device == global_one_shot_timer || !(device->flags & TIMER_PERIODIC)) {
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
    if (device == global_periodic_timer || !(device->flags & TIMER_ONE_SHOT)) {
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

  cond_init(&alarm_cond, 0);
  spin_init(&alarm_cond_lock);
  thread_create(alarm_event_loop, NULL);
}

clockid_t timer_create_alarm(clock_t expires, timer_cb_t callback, void *data) {
  if (expires < clock_now()) {
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

  // check if timer needs to be updated
  if (alarm == pending_alarm_tree->min->data) {
    // set timer to new most-recently expiring alarm expiry
    int result = set_alarm_timer_value(global_one_shot_timer, alarm->expires);
    if (result < 0) {
      panic("failed to set alarm timer value");
    }
  }

  spin_unlock(&pending_alarm_lock);
  return alarm->id;
}

void *timer_delete_alarm(clockid_t id) {
  spin_lock(&pending_alarm_lock);
  timer_alarm_t *alarm = rb_tree_delete(pending_alarm_tree, id);
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
