//
// Created by Aaron Gill-Braun on 2020-08-24.
//

#include <kernel/cpu/asm.h>
#include <kernel/cpu/cpu.h>
#include <kernel/cpu/rtc.h>

static void rtc_callback(registers_t regs) {
}

void init_rtc() {
  // /* Install the function we just wrote */
  // register_interrupt_handler(IRQ0, rtc_callback);
  //
  // /* Get the PIT value: hardware clock at 1193180 Hz */
  // uint32_t divisor = 1193180 / freq;
  // uint8_t low = (uint8_t)(divisor & 0xFF);
  // uint8_t high = (uint8_t)((divisor >> 8) & 0xFF);
  // /* Send the command */
  // outb(0x43, 0x36); /* Command port */
  // outb(0x40, low);
  // outb(0x40, high);
}
