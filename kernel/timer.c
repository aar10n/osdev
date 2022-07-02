//
// Created by Aaron Gill-Braun on 2020-10-20.
//

#include <timer.h>

#include <mm.h>
#include <thread.h>
#include <mutex.h>
#include <spinlock.h>
#include <printf.h>
#include <panic.h>

#include <atomic.h>
#include <rb_tree.h>

extern void hpet_handler();
static id_t __id;
static rb_tree_t *tree;
static rb_tree_t *lookup_tree;
static cond_t event;
// static rw_spinlock_t lock;

timer_device_t *global_timer_device;
timer_device_t *global_periodic_timer;
timer_device_t *global_one_shot_timer;

LIST_HEAD(timer_device_t) timer_devices;

void register_timer_device(timer_device_t *device) {
  kassert(device != NULL);
  if ((device->flags & TIMER_ONE_SHOT) == 0 && (device->flags & TIMER_PERIODIC) == 0) {
    panic("timer device '%s' must support either one-shot or periodic mode", device->name);
  }

  LIST_ENTRY_INIT(&device->list);
  LIST_ADD(&timer_devices, device, list);

  kprintf("timer: registering timer device '%s'\n", device->name);
}

//

void timer_init() {
  timer_device_t *device = NULL;
  global_timer_device = LIST_FIRST(&timer_devices);
  LIST_FOREACH(device, &timer_devices, list) {
    if (global_periodic_timer != NULL && global_one_shot_timer != NULL) {
      break;
    }

    if (global_periodic_timer == NULL && device->flags & TIMER_PERIODIC) {
      global_periodic_timer = device;
    } else if (global_one_shot_timer == NULL && device->flags & TIMER_ONE_SHOT) {
      global_one_shot_timer = device;
    }
  }

  // kprintf("timer: initializing timer '%s'\n", global_periodic_timer->name);
  // int result;
  // result = global_periodic_timer->init(global_periodic_timer, TIMER_PERIODIC);
  // kassert(result == 0);
  //
  // global_periodic_timer->setval(global_periodic_timer, 1e9);
  //
  // kprintf("timer: enabling timer '%s'\n", global_periodic_timer->name);
  // global_periodic_timer->enable(global_periodic_timer);
}

//

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
