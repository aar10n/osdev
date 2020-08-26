//
// Created by Aaron Gill-Braun on 2019-04-18.
//


#ifndef KERNEL_CPU_IDT_H
#define KERNEL_CPU_IDT_H

#include <stdint.h>

#define KERNEL_CS 0x08
#define IDT_ENTRIES 256

#define low_16(address) (uint16_t)((address) &0xFFFF)
#define high_16(address) (uint16_t)(((address) >> 16) & 0xFFFF)

// IDT Gate Types
#define TASK_GATE_32 0x5
#define INTERRUPT_GATE_16 0x6
#define TRAP_GATE_16 0x7
#define INTERRUPT_GATE_32 0xE
#define TRAP_GATE_32 0xF

typedef struct __attribute__((packed)) {
  uint8_t gate_type : 4;       // The IDT gate type
  uint8_t storage_segment : 1; // Storage segment (0 for interrupt and trap gates)
  uint8_t privilege_level : 2; // Descriptor privilege level (0=kernel..3=user)
  uint8_t present : 1;         // Present (0 for unused interrupts)
} idt_gate_attr_t;

typedef struct __attribute__((packed)) {
  uint16_t low_offset;  // Bits 0..15 of the handler function address
  uint16_t selector;    // Code segment selector in GDT or LDT
  uint8_t zero;         // Always set to 0
  idt_gate_attr_t attr; // Attributes
  uint16_t high_offset; // Bits 16..31 of the handler function address
} idt_gate_t;

typedef struct __attribute__((packed)) {
  uint16_t limit;
  uint32_t base;
} idt_register_t;

idt_gate_t idt[IDT_ENTRIES];
idt_register_t idt_reg;

void install_idt();
void set_idt_gate(int vector, uint32_t handler);

#endif // KERNEL_CPU_IDT_H
