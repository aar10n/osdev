//
// Created by Aaron Gill-Braun on 2019-04-18.
//

#include <stdint.h>

#ifndef KERNEL_CPU_IDT_H
#define KERNEL_CPU_IDT_H

#define KERNEL_CS 0x08
#define IDT_ENTRIES 256

#define low_16(address) (uint16_t)((address) & 0xFFFF)
#define high_16(address) (uint16_t)(((address) >> 16) & 0xFFFF)

typedef struct {
  uint16_t low_offset; /* Lower 16 bits of handler function address */
  uint16_t sel; /* Kernel segment selector */
  uint8_t always0;
  /* First byte
   * Bit 7: "Interrupt is present"
   * Bits 6-5: Privilege level of caller (0=kernel..3=user)
   * Bit 4: Set to 0 for interrupt gates
   * Bits 3-0: bits 1110 = decimal 14 = "32 bit interrupt gate" */
  uint8_t flags;
  uint16_t high_offset; /* Higher 16 bits of handler function address */
} __attribute__((packed)) idt_gate_t;

typedef struct {
  uint16_t limit;
  uint32_t base;
} __attribute__((packed)) idt_register_t;

idt_gate_t idt[IDT_ENTRIES];
idt_register_t idt_reg;


void set_idt_gate(int n, uint32_t handler);
void install_idt();

#endif //KERNEL_CPU_IDT_H
