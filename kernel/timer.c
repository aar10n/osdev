//
// Created by Aaron Gill-Braun on 2020-10-20.
//

#include <timer.h>
#include <atomic.h>
#include <percpu.h>
#include <vectors.h>
#include <cpu/idt.h>
#include <mm/heap.h>
#include <device/ioapic.h>
#include <device/hpet.h>

#include <mutex.h>
#include <spinlock.h>
#include <printf.h>
#include <rb_tree.h>
#include <thread.h>

extern void hpet_handler();
static id_t __id;
static rb_tree_t *tree;
static rb_tree_t *lookup_tree;
static cond_t event;
// static rw_spinlock_t lock;

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
}

noreturn void *timer_event_loop(void *arg) {
  while (true) {
    cond_wait(&event);

    timer_event_t *timer = tree->min->data;
    clock_t expiry = timer->expiry;
    if (/*lock.locked */false || timer->cpu != PERCPU->id) {
      continue;
    }

    label(dispatch);
    current_thread->preempt_count++;
    timer->callback(timer->data);
    current_thread->preempt_count--;

    // spinrw_aquire_write(&lock);
    rb_tree_delete_node(tree, tree->min);
    rb_tree_delete(lookup_tree, timer->id);
    // spinrw_release_write(&lock);

    kfree(timer);
    timer = tree->min->data;
    if (timer && timer->cpu == PERCPU->id && timer->expiry == expiry) {
      goto dispatch;
    } else if (timer) {
      hpet_set_timer(timer->expiry);
    }
  }
}

//

uint64_t timer_now() {
  return hpet_get_time();
}

void timer_init() {
  hpet_init();
  ioapic_set_irq(0, 2, VECTOR_HPET_TIMER);
  idt_gate_t gate = gate((uintptr_t) hpet_handler, KERNEL_CS, 0, INTERRUPT_GATE, 0, 1);
  idt_set_gate(VECTOR_HPET_TIMER, gate);

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
  timer->cpu = PERCPU->id;
  timer->expiry = ns;
  timer->callback = callback;
  timer->data = data;

  if (tree->min == tree->nil || ns < tree->min->key) {
    hpet_set_timer(ns);
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
