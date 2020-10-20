//
// Created by Aaron Gill-Braun on 2019-04-18.
//


#ifndef KERNEL_CPU_IDT_H
#define KERNEL_CPU_IDT_H

#include <base.h>
#include <cpu/cpu.h>

#define IDT_GATES 256
#define IDT_STUB_SIZE 32

// IDT Gate Types
#define CALL_GAGE 0xC
#define INTERRUPT_GATE 0xE
#define TRAP_GATE 0xF

#define gate(offset, selector, ist, type, dpl, p)   \
  ((idt_gate_t) {                                   \
    (offset) & 0xFFFF, selector, ist, type, dpl, p, \
    (offset) >> 16, (offset) >> 32                  \
  })

typedef struct packed {
  uint16_t low_offset;   // low 16 bits of the isr address
  uint16_t selector;     // segment selector for dest code segment
  uint16_t ist : 3;      // interrupt stack table
  uint16_t : 5;          // reserved
  uint16_t type: 4;      // gate type
  uint16_t : 1;          // reserved
  uint16_t dpl : 2;      // descriptor privilege level
  uint16_t present : 1;  // segment present
  uint16_t mid_offset;   // mid 16 bits of the isr address
  uint32_t high_offset;  // high 32 bits of the isr address
  uint32_t : 32;         // reserved
} idt_gate_t;

typedef struct packed {
  uint16_t limit;
  uint64_t base;
} idt_desc_t;

typedef void (*idt_handler_t)();

void setup_idt();
void idt_set_gate(uint8_t vector, idt_gate_t gate);
void idt_hook(uint8_t vector, idt_handler_t handler);
void idt_unhook(uint8_t vector);

#endif // KERNEL_CPU_IDT_H
