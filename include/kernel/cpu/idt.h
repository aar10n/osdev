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

#define gate(off, sel, istn, typ, ring, p)             \
  ((struct idt_gate) {                                \
    .low_offset = (off) & 0xFFFF, .selector = (sel),  \
    .ist = (istn), .type = (typ), .dpl = (ring), .present = (p), \
    .mid_offset = ((off) >> 16) & 0xFFFF, \
    .high_offset = ((off) >> 32) & 0xFFFFFFFF \
  })

struct idt_gate {
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
};
static_assert(sizeof(struct idt_gate) == 16);

struct packed idt_desc {
  uint16_t limit;
  uint64_t base;
};
static_assert(sizeof(struct idt_desc) == 10);

void idt_set_gate_ist(uint8_t num, uint8_t ist);

#endif // KERNEL_CPU_IDT_H
