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

void scheduler_tick();

static id_t __id;
static rb_tree_t *tree;
static rb_tree_t *lookup_tree;
static cond_t event;
// static rw_spinlock_t lock;

timer_device_t *global_timer_device;
timer_device_t *global_periodic_timer;
timer_device_t *global_one_shot_timer;

LIST_HEAD(timer_device_t) timer_devices;


void timer_periodic_handler(timer_device_t *td) {
  kprintf("---> tick <---\n");
  clock_update_ticks();
  scheduler_tick();
}

void timer_oneshot_handler(timer_device_t *td) {
  kprintf("---> one-shot timer <---\n");
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
    // a periodic timer has already been selected
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

int timer_setval(uint16_t type, uint64_t value) {
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

  return td->setval(td, value);
}

static inline id_t alloc_id() {
  return atomic_fetch_add(&__id, 1);
}

//

void handle_insert_node(rb_tree_t *_tree, rb_node_t *node) {
  // spinrw_aquire_write(&lock);
  timer_event_t *timer = node->data;
  rb_tree_insert(lookup_tree, timer->id, node);
  // spinrw_release_write(&lock);
}

static rb_tree_events_t events = {
  .post_insert_node = handle_insert_node
};

//

void timer_handler() {
  cond_signal(&event);
  // kprintf("interrupting thread %d process %d\n", gettid(), getpid());
  // thread_yield();
}

noreturn void *timer_event_loop(void *arg) {
  while (true) {
    cond_wait(&event);

    timer_event_t *timer = tree->min->data;
    clock_t expiry = timer->expiry;
    if (/*lock.locked */false || timer->cpu != PERCPU_ID) {
      continue;
    }

  dispatch:;
    PERCPU_THREAD->preempt_count++;
    timer->callback(timer->data);
    PERCPU_THREAD->preempt_count--;

    // spinrw_aquire_write(&lock);
    rb_tree_delete_node(tree, tree->min);
    rb_tree_delete(lookup_tree, timer->id);
    // spinrw_release_write(&lock);

    kfree(timer);
    timer = tree->min->data;
    if (timer && timer->cpu == PERCPU_ID && timer->expiry == expiry) {
      goto dispatch;
    } else if (timer) {

    }
  }
}

//

uint64_t timer_now() {
  return 0;
}

void __timer_init() {
  // ioapic_set_irq(0, 2, VECTOR_HPET_TIMER);
  // idt_gate_t gate = gate((uintptr_t) hpet_handler, KERNEL_CS, 0, INTERRUPT_GATE, 0, 1);
  // idt_set_gate(VECTOR_HPET_TIMER, gate);

  tree = create_rb_tree();
  tree->events = &events;

  lookup_tree = create_rb_tree();

  cond_init(&event, 0);
  thread_create(timer_event_loop, NULL);
  // spinrw_init(&lock);
}

id_t create_timer(clock_t ns, timer_cb_t callback, void *data) {
  timer_event_t *timer = kmalloc(sizeof(timer_event_t));
  timer->id = alloc_id();
  timer->cpu = PERCPU_ID;
  timer->expiry = ns;
  timer->callback = callback;
  timer->data = data;

  if (tree->min == tree->nil || ns < tree->min->key) {

  }

  // spinrw_aquire_write(&lock);
  rb_tree_insert(tree, ns, timer);
  // spinrw_release_write(&lock);
  return timer->id;
}

void *timer_cancel(id_t id) {
  // spinrw_aquire_write(&lock);
  rb_node_t *node = rb_tree_find(lookup_tree, id);
  if (node == NULL) {
    // spinrw_release_write(&lock);
    return NULL;
  }

  timer_event_t *timer = rb_tree_delete_node(tree, node->data);
  rb_tree_delete_node(lookup_tree, node);
  // spinrw_release_write(&lock);
  void *data = timer->data;
  kfree(timer);
  return data;
}

void timer_print_debug() {
  // spinrw_aquire_read(&lock);
  rb_iter_t *iter = rb_tree_iter(tree);
  rb_node_t *node;
  while ((node = rb_iter_next(iter))) {
    kprintf("timer %d | %s\n", node->key, ((timer_event_t *) node->data)->data);
  }
  kfree(iter);
  // spinrw_release_read(&lock);
}
