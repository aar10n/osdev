//
// Created by Aaron Gill-Braun on 2019-04-19.
//

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <kernel/cpu/interrupt.h>
#include <kernel/cpu/pit.h>

#include <kernel/cpu/asm.h>
#include <kernel/cpu/cpu.h>

volatile uint32_t tick = 0;
uint32_t frequency = 1193181;
uint32_t divisor = 1;

static void pit_irq_handler(registers_t regs) {
  tick++;
}

void pit_init() {
  /* Install the function we just wrote */
  register_isr(IRQ0, pit_irq_handler);

  uint8_t low = (uint8_t)(divisor & 0xFF);
  uint8_t high = (uint8_t)((divisor >> 8) & 0xFF);
  /* Send the command */
  outb(0x43, 0x36); /* Command port */
  outb(0x40, low);
  outb(0x40, high);
}
