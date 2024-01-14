//
// Created by Aaron Gill-Braun on 2020-08-25.
//

#include <kernel/cpu/idt.h>
#include <kernel/cpu/cpu.h>
#include <kernel/mm.h>

#define IDT_GATES 256
#define IDT_STUB_SIZE 32

extern uintptr_t __isr_stubs;
struct idt_gate shared_idt[IDT_GATES];

void idt_percpu_init() {
  struct idt_gate *idt = shared_idt;
  if (curcpu_is_boot) {
    // first time - initialize the IDT with the common stubs
    uintptr_t asm_handler = (uintptr_t) &__isr_stubs;
    for (int i = 0; i < IDT_GATES; i++) {
      shared_idt[i] = gate(asm_handler, KERNEL_CS, 0, INTERRUPT_GATE, 0, 1);
      asm_handler += IDT_STUB_SIZE;
    }
  }

  struct idt_desc desc;
  desc.base = (uintptr_t) idt;
  desc.limit = sizeof(shared_idt) - 1;
  cpu_load_idt(&desc);
}
PERCPU_EARLY_INIT(idt_percpu_init);

//

void idt_set_gate_ist(uint8_t num, uint8_t ist) {
  struct idt_gate *gate = shared_idt + num;
  gate->ist = ist;
}
