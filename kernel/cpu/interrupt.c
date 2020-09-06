//
// Created by Aaron Gill-Braun on 2020-08-25.
//

#include <stddef.h>

#include <kernel/cpu/asm.h>
#include <kernel/cpu/cpu.h>
#include <kernel/cpu/interrupt.h>

isr_t interrupt_handlers[256];

void register_isr(uint8_t interrupt, isr_t handler) {
  interrupt_handlers[interrupt] = handler;
}

void unregister_isr(uint8_t interrupt) {
  interrupt_handlers[interrupt] = NULL;
}

void interrupt_handler(registers_t reg) {
  // Send EOI to PIC
  if (reg.int_no >= 40) outb(0xA0, 0x20); /* slave */
  outb(0x20, 0x20);                       /* master */


  isr_t handler = interrupt_handlers[reg.int_no];
  if (handler) {
    handler(reg);
  }
}

