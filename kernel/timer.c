//
// Created by Aaron Gill-Braun on 2020-10-20.
//

#include <timer.h>
#include <atomic.h>
#include <vectors.h>
#include <cpu/idt.h>
#include <mm/heap.h>
#include <device/ioapic.h>
#include <device/hpet.h>

#include <rb_tree.h>
#include <stdio.h>

extern void hpet_handler();
static uint64_t __id;
static rb_tree_t *tree;

static inline uint64_t alloc_id() {
  return atomic_fetch_add(&__id, 1);
}

void timer_handler() {
  kprintf("timer irq!\n");
  timer_t *timer = tree->min->data;
  clock_t expiry = timer->expiry;

  label(dispatch);
  timer->callback(timer->data);
  rb_tree_delete_node(tree, tree->min);
  kfree(timer);

  timer = tree->min->data;
  if (timer && timer->expiry == expiry) {
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
}

void create_timer(clock_t ns, timer_cb_t callback, void *data) {
  timer_t *timer = kmalloc(sizeof(timer_t));
  timer->id = alloc_id();
  timer->expiry = ns;
  timer->callback = callback;
  timer->data = data;

  if (tree->min == tree->nil || ns < tree->min->key) {
    hpet_set_timer(ns);
  }
  rb_tree_insert(tree, ns, timer);
}

void timer_print_debug() {
  rb_iter_t *iter = rb_tree_iter(tree);
  rb_node_t *node;
  while ((node = rb_iter_next(iter))) {
    kprintf("timer %d | %s\n", node->key, ((timer_t *) node->data)->data);
  }
  kfree(iter);
}
