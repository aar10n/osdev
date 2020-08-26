//
// Created by Aaron Gill-Braun on 2020-08-25.
//

#include <kernel/cpu/asm.h>
#include <kernel/cpu/cpu.h>
#include <kernel/cpu/interrupt.h>

isr_t interrupt_handlers[256];

void register_interrupt_handler(uint8_t num, isr_t handler) {
  interrupt_handlers[num] = handler;
}

void interrupt_handler(registers_t reg) {
  // Send EOI to PIC
  if (reg.int_no >= 40) outb(0xA0, 0x20); /* slave */
  outb(0x20, 0x20);                       /* master */

  if (interrupt_handlers[reg.int_no] != 0) {
    isr_t handler = interrupt_handlers[reg.int_no];
    handler(reg);
  }
}

