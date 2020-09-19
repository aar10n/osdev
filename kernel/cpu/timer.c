//
// Created by Aaron Gill-Braun on 2019-04-19.
//

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <kernel/cpu/interrupt.h>
#include <kernel/cpu/timer.h>

#include <kernel/cpu/asm.h>
#include <kernel/cpu/cpu.h>
#include <kernel/task.h>

uint32_t tick = 0;

static void timer_irq_handler(registers_t regs) {
  tick++;
  // kprintf("tick: %d\n", tick);
  task_switch();
}

void init_timer(uint32_t freq) {
  /* Install the function we just wrote */
  register_isr(IRQ0, timer_irq_handler);

  /* Get the PIT value: hardware clock at 1193180 Hz */
  uint32_t divisor = 1193180 / freq;
  uint8_t low = (uint8_t)(divisor & 0xFF);
  uint8_t high = (uint8_t)((divisor >> 8) & 0xFF);
  /* Send the command */
  outb(0x43, 0x36); /* Command port */
  outb(0x40, low);
  outb(0x40, high);
}
