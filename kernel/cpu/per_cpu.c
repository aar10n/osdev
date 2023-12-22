//
// Created by Aaron Gill-Braun on 2022-06-25.
//

#include <kernel/cpu/per_cpu.h>
#include <kernel/mm.h>
#include <kernel/panic.h>


per_cpu_t *percpu_alloc_area(uint16_t id, uint8_t apic_id) {
  page_t *pages = alloc_pages(SIZE_TO_PAGES(PERCPU_SIZE));
  kassert(pages != NULL);
  uintptr_t vaddr = vmap_pages(pages, 0, PERCPU_SIZE, VM_WRITE, "percpu data");
  kassert(vaddr != 0);

  per_cpu_t *percpu = (void *) vaddr;
  memset(percpu, 0, PERCPU_SIZE);
  percpu->self = (uintptr_t) percpu;
  percpu->id = id;
  percpu->apic_id = apic_id;
  return percpu;
}
