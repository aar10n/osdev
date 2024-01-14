//
// Created by Aaron Gill-Braun on 2023-12-28.
//

#include <kernel/percpu.h>
#include <kernel/mm.h>

struct percpu *percpu_alloc_area(uint32_t id) {
  struct percpu *area = kmallocz(sizeof(struct percpu));
  area->id = id;
  area->self = (uintptr_t) area;
  return area;
}
