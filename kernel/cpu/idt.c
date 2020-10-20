//
// Created by Aaron Gill-Braun on 2020-08-25.
//

#include <stdio.h>
#include <cpu/cpu.h>
#include <cpu/idt.h>

#include <device/apic.h>

extern uintptr_t idt_stubs;

idt_gate_t idt[IDT_GATES];
idt_desc_t idt_desc;
idt_handler_t idt_handlers[IDT_GATES];

//

__used void irq_handler(uint8_t vector) {
  idt_handler_t handler = idt_handlers[vector];
  if (handler) {
    handler();
  }
  apic_send_eoi();
}


void setup_idt() {
  uintptr_t offset = (uintptr_t) &idt_stubs;
  for (int i = 0; i < IDT_GATES; i++) {;
    idt_gate_t gate = gate(offset, KERNEL_CS, 0, INTERRUPT_GATE, 0, 1);
    idt[i] = gate;
    offset += IDT_STUB_SIZE;
  }

  idt_desc.limit = sizeof(idt) - 1;
  idt_desc.base = (uint64_t) &idt;
  load_idt(&idt_desc);
}

void idt_set_gate(uint8_t vector, idt_gate_t gate) {
  idt[vector] = gate;
}

void idt_hook(uint8_t vector, idt_handler_t handler) {
  if (idt_handlers[vector] != NULL) {
    kprintf("[idt] overriding handler on vector %d\n", vector);
  }
  idt_handlers[vector] = handler;
}

void idt_unhook(uint8_t vector) {
  if (idt_handlers[vector] == NULL) {
    kprintf("[idt] no handler to unhook on vector %d\n", vector);
  }
  idt_handlers[vector] = NULL;
}
