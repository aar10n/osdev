//
// Created by Aaron Gill-Braun on 2020-08-25.
//

#include <kernel/cpu/idt.h>
#include <kernel/cpu/cpu.h>

#define IDT_GATES 256
#define IDT_STUB_SIZE 32

typedef struct packed {
  uint16_t limit;
  uint64_t base;
} idt_desc_t;

idt_gate_t idt[IDT_GATES];
idt_desc_t idt_desc;

extern uintptr_t idt_stubs;

void setup_idt() {
  if (PERCPU_IS_BOOT) {
    // setup the shared IDT
    uintptr_t asm_handler = (uintptr_t) &idt_stubs;
    for (int i = 0; i < IDT_GATES; i++) {;
      idt[i] = gate(asm_handler, KERNEL_CS, 0, INTERRUPT_GATE, 0, 1);
      asm_handler += IDT_STUB_SIZE;
    }

    idt_desc.base = (uintptr_t) idt;
    idt_desc.limit = sizeof(idt) - 1;
  }
  cpu_load_idt(&idt_desc);
}

void set_gate_ist(uint8_t num, uint8_t ist) {
  idt[num].ist = ist;
}
