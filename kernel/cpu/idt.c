//
// Created by Aaron Gill-Braun on 2020-08-25.
//

#include <cpu/idt.h>
#include <cpu/cpu.h>

#include <string.h>
#include <printf.h>

#define IDT_GATES 256
#define IDT_STUB_SIZE 32

typedef struct packed {
  uint16_t limit;
  uint64_t base;
} idt_desc_t;

idt_gate_t idt[IDT_GATES];
idt_handler_t idt_handlers[IDT_GATES];
idt_desc_t idt_desc;

extern uintptr_t idt_stubs;
extern void page_fault_handler();
// extern void sched_irq_hook(uint8_t vector);

void setup_idt() {
  memset((void *) idt, 0, sizeof(idt));

  uintptr_t asm_handler = (uintptr_t) &idt_stubs;
  for (int i = 0; i < IDT_GATES; i++) {;
    idt[i] = gate(asm_handler, KERNEL_CS, 0, INTERRUPT_GATE, 0, 1);
    asm_handler += IDT_STUB_SIZE;
  }

  // page fault handler
  // idt[VECTOR_PAGE_FAULT] = gate((uintptr_t) page_fault_handler, KERNEL_CS, 0, INTERRUPT_GATE, 0, 1);

  idt_desc.base = (uintptr_t) idt;
  idt_desc.limit = sizeof(idt) - 1;
  load_idt(&idt_desc);
}

void idt_set_gate(uint8_t vector, idt_gate_t gate) {
  idt[vector] = gate;
}

void idt_hook(uint8_t vector, idt_function_t fn, void *data) {
  kprintf("IDT: irq %d hook\n", vector);
  if (idt_handlers[vector].fn != NULL) {
    kprintf("[idt] overriding handler on vector %d\n", vector);
  }

  idt_handlers[vector].fn = fn;
  idt_handlers[vector].data = data;
}

void *idt_unhook(uint8_t vector) {
  kprintf("IDT: irq %d unhook\n", vector);
  if (idt_handlers[vector].fn == NULL) {
    kprintf("[idt] no handler to unhook on vector %d\n", vector);
  }
  void *ptr = idt_handlers[vector].data;
  idt_handlers[vector].fn = NULL;
  idt_handlers[vector].data = NULL;
  return ptr;
}
