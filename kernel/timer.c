//
// Created by Aaron Gill-Braun on 2020-10-20.
//

#include <kernel/timer.h>

#include <kernel/mm.h>
#include <kernel/clock.h>
#include <kernel/mutex.h>
#include <kernel/printf.h>
#include <kernel/panic.h>

timer_device_t *global_periodic_timer;
timer_device_t *global_one_shot_timer;

LIST_HEAD(timer_device_t) timer_devices;


void timer_periodic_handler(timer_device_t *td) {
  kprintf("---> tick <---\n");
  // clock_update_ticks();
  // panic("sched_tick not implemented");
}

void timer_oneshot_handler(timer_device_t *td) {
  // if (mtx_trylock(&alarm_cond_lock)) {
  //   // clock_update_ticks();
  //   // cv_signal(&alarm_cond);
  //   mtx_unlock(&alarm_cond_lock);
  // }
}


void register_timer_device(timer_device_t *device) {
  kassert(device != NULL);
  mtx_init(&device->lock, MTX_SPIN, "timer_device_lock");
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
  todo();
}
