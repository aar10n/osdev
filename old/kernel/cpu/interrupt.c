//
// Created by Aaron Gill-Braun on 2020-08-25.
//

#include <stddef.h>

#include <kernel/cpu/apic.h>
#include <kernel/cpu/cpu.h>
#include <kernel/cpu/interrupt.h>
#include <kernel/cpu/pic.h>

isr_t interrupt_handlers[256];

void register_isr(uint8_t interrupt, isr_t handler) {
  interrupt_handlers[interrupt] = handler;
}

void unregister_isr(uint8_t interrupt) {
  interrupt_handlers[interrupt] = NULL;
}

void interrupt_handler(registers_t reg) {
  // pic_send_eoi(reg.int_no);

  isr_t handler = interrupt_handlers[reg.int_no];
  if (handler) {
    handler(reg);
  }

  apic_send_eoi();
}

