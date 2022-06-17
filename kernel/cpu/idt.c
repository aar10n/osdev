//
// Created by Aaron Gill-Braun on 2020-08-25.
//

#include <cpu/idt.h>
#include <cpu/cpu.h>

#include <mm/init.h>
#include <device/apic.h>
#include <vectors.h>
#include <printf.h>

extern uintptr_t idt_stubs;
extern void page_fault_handler();
// extern void sched_irq_hook(uint8_t vector);

//

__used void irq_handler(uint8_t vector, regs_t *regs) {
  // apic_send_eoi();
  kprintf("IRQ %d\n", vector);
  idt_handler_t handler = IDT_HANDLERS[vector];
  if (handler.fn) {
    handler.fn(vector, handler.data);
  }
  // sched_irq_hook(vector);
}

void setup_idt() {
  size_t idt_page_count = SIZE_TO_PAGES(sizeof(idt_t));
  uintptr_t pages = mm_early_alloc_pages(idt_page_count);
  memset((void *) pages, 0, sizeof(idt_t));

  idt_t *idt = (void *) pages;
  PERCPU->idt = idt;

  uintptr_t asm_handler = (uintptr_t) &idt_stubs;
  idt_gate_t *idt_gates = idt->idt;
  for (int i = 0; i < IDT_GATES; i++) {;
    // idt_gates[i] = gate(asm_handler, KERNEL_CS, 0, INTERRUPT_GATE, 0, 1);
    idt_gates[i] = gate(asm_handler, 0x38, 0, INTERRUPT_GATE, 0, 1);
    asm_handler += IDT_STUB_SIZE;
  }

  // page fault handler
  // idt_gates[VECTOR_PAGE_FAULT] = gate((uintptr_t) page_fault_handler, KERNEL_CS, 0, INTERRUPT_GATE, 0, 1);
  idt_gates[VECTOR_PAGE_FAULT] = gate((uintptr_t) page_fault_handler, 0x38, 0, INTERRUPT_GATE, 0, 1);

  idt->desc.limit = (sizeof(idt_gate_t) * IDT_GATES) - 1;
  idt->desc.base = (uintptr_t) idt_gates;
  load_idt(&idt->desc);
}

void idt_set_gate(uint8_t vector, idt_gate_t gate) {
  IDT[vector] = gate;
}

void idt_hook(uint8_t vector, idt_function_t fn, void *data) {
  if (IDT_HANDLERS[vector].fn != NULL) {
    kprintf("[idt] overriding handler on vector %d\n", vector);
  }
  IDT_HANDLERS[vector].fn = fn;
  IDT_HANDLERS[vector].data = data;
}

void *idt_unhook(uint8_t vector) {
  if (IDT_HANDLERS[vector].fn == NULL) {
    kprintf("[idt] no handler to unhook on vector %d\n", vector);
  }
  void *ptr = IDT_HANDLERS[vector].data;
  IDT_HANDLERS[vector].fn = NULL;
  IDT_HANDLERS[vector].data = NULL;
  return ptr;
}
