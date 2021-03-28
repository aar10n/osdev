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

#include <rb_tree.h>
#include <printf.h>
#include <spinlock.h>

extern void hpet_handler();
static uint64_t __id;
static rb_tree_t *tree;
// static rw_spinlock_t lock;

static inline uint64_t alloc_id() {
  return atomic_fetch_add(&__id, 1);
}

void timer_handler() {
  // kprintf("timer irq!\n");
  timer_event_t *timer = tree->min->data;
  clock_t expiry = timer->expiry;
  if (/*lock.locked */false || timer->cpu != PERCPU->id) {
    return;
  }

  label(dispatch);
  timer->callback(timer->data);

  // spinrw_aquire_write(&lock);
  rb_tree_delete_node(tree, tree->min);
  // spinrw_release_write(&lock);

  timer = tree->min->data;
  if (timer && timer->cpu == PERCPU->id && timer->expiry == expiry) {
    goto dispatch;
  } else if (timer) {
    hpet_set_timer(timer->expiry);
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
  // spinrw_init(&lock);
}

void create_timer(clock_t ns, timer_cb_t callback, void *data) {
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
