//
// Created by Aaron Gill-Braun on 2023-12-28.
//

#include <kernel/percpu.h>
#include <kernel/mm.h>
#include <kernel/cpu/cpu.h>

// symbols added to aid in debugging
_used struct proc **current_proc = NULL;
_used struct thread **current_thread = NULL;

static void percpu_early_init() {
  current_proc = &curcpu_area->proc;
  current_thread = &curcpu_area->thread;
}
EARLY_INIT(percpu_early_init);

struct percpu *percpu_alloc_area(uint32_t id) {
  size_t size = page_align(sizeof(struct percpu));
  page_t *pages = alloc_pages(SIZE_TO_PAGES(size));
  if (pages == NULL) {
    panic("percpu_alloc_area: failed to allocate pages for percpu area");
  }

  // map the pages to a virtual address space
  uintptr_t virt_addr = vmap_pages(moveref(pages), 0, size, VM_WRITE | VM_GLOBAL, "percpu area");
  if (virt_addr == 0) {
    panic("percpu_alloc_area: failed to map percpu area pages");
  }

  // allocate an irq stack now
  page_t *irq_stack = alloc_pages(SIZE_TO_PAGES(IRQ_STACK_SIZE));
  kassert(irq_stack != NULL);
  uintptr_t irq_stack_addr = vmap_pages(moveref(irq_stack), 0, IRQ_STACK_SIZE, VM_WRITE | VM_STACK, "irq stack");

  struct percpu *area = (void *) virt_addr;
  area->id = id;
  area->self = (uintptr_t) area;
  area->irq_stack_top = irq_stack_addr + IRQ_STACK_SIZE;
  return area;
}
