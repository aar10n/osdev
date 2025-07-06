//
// Created by Aaron Gill-Braun on 2023-12-28.
//

#include <kernel/percpu.h>
#include <kernel/mm.h>

// symbols added to aid in debugging
_used struct proc **current_proc = NULL;
_used struct thread **current_thread = NULL;

static void percpu_early_init() {
  current_proc = &curcpu_area->proc;
  current_thread = &curcpu_area->thread;
}
EARLY_INIT(percpu_early_init);

struct percpu *percpu_alloc_area(uint32_t id) {
  struct percpu *area = kmallocz(sizeof(struct percpu));
  area->id = id;
  area->self = (uintptr_t) area;
  return area;
}
