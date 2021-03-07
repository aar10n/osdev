//
// Created by Aaron Gill-Braun on 2020-08-25.
//

#include <percpu.h>
#include <cpu/cpu.h>
#include <cpu/idt.h>
#include <printf.h>

#include <device/apic.h>

extern uintptr_t idt_stubs;

//

__used void irq_handler(uint8_t vector, regs_t *regs) {
  idt_handler_t handler = IDT_HANDLERS[vector];
  if (handler) {
    handler(regs);
  }
  apic_send_eoi();
}


void setup_idt() {
  uintptr_t offset = (uintptr_t) &idt_stubs;
  idt_t *idt_struct = PERCPU->idt;

  idt_gate_t *idt = idt_struct->idt;
  for (int i = 0; i < IDT_GATES; i++) {;
    idt_gate_t gate = gate(offset, KERNEL_CS, 0, INTERRUPT_GATE, 0, 1);
    idt[i] = gate;
    offset += IDT_STUB_SIZE;
  }

  idt_struct->desc.limit = sizeof(idt_struct->idt)- 1;
  idt_struct->desc.base = (uint64_t) &idt_struct->idt;
  load_idt(&(idt_struct->desc));
}

void idt_set_gate(uint8_t vector, idt_gate_t gate) {
  IDT[vector] = gate;
}

void idt_hook(uint8_t vector, idt_handler_t handler) {
  if (IDT_HANDLERS[vector] != NULL) {
    kprintf("[idt] overriding handler on vector %d\n", vector);
  }
  IDT_HANDLERS[vector] = handler;
}

void idt_unhook(uint8_t vector) {
  if (IDT_HANDLERS[vector] == NULL) {
    kprintf("[idt] no handler to unhook on vector %d\n", vector);
  }
  IDT_HANDLERS[vector] = NULL;
}
