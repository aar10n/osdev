//
// Created by Aaron Gill-Braun on 2019-04-18.
//

#ifndef KERNEL_CPU_IDT_H
#define KERNEL_CPU_IDT_H

#include <kernel/base.h>

// IDT Gate Types
#define CALL_GAGE 0xC
#define INTERRUPT_GATE 0xE
#define TRAP_GATE 0xF

#define gate(offset, selector, ist, type, dpl, p)   \
  ((idt_gate_t) {                                   \
    (offset) & 0xFFFF, selector, ist, type, dpl, p, \
    (offset) >> 16, (offset) >> 32                  \
  })

typedef struct packed idt_gate {
  uint64_t low_offset : 16;  // low 16 bits of the isr address
  uint64_t selector : 16;    // segment selector for dest code segment
  uint64_t ist : 3;          // interrupt stack table
  uint64_t : 5;              // reserved
  uint64_t type : 4;         // gate type
  uint64_t : 1;              // reserved
  uint64_t dpl : 2;          // descriptor privilege level
  uint64_t present : 1;      // segment present
  uint64_t mid_offset : 16;  // mid 16 bits of the isr address
  uint64_t high_offset : 32; // high 32 bits of the isr address
  uint64_t : 32;             // reserved
} idt_gate_t;
static_assert(sizeof(idt_gate_t) == 16);

typedef void (*idt_function_t)(uint8_t, void *);

void setup_idt();

#endif // KERNEL_CPU_IDT_H
