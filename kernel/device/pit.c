//
// Created by Aaron Gill-Braun on 2020-10-15.
//

#include <panic.h>
#include <vectors.h>
#include <cpu/io.h>
#include <cpu/idt.h>
#include <device/pit.h>
#include <stdio.h>

#define PIT0_DATA   0x40
#define PIT1_DATA   0x41
#define PIT2_DATA   0x42
#define PIT2_GATE   0x61
#define PIT_CONTROL 0x43

#define pit_control(bcd, mode, access, channel) \
  ((bcd) | ((mode) << 1) | ((access) << 4) | ((channel) << 6))

#define pit_readback(ch0, ch1, ch2, latsts, latcnt) \
  (((ch0) << 1) | ((ch1) << 2) | ((ch2) << 3) | ((latsts) << 4) \
  ((latcnt) << 5) | (0b11 << 6))


static int timer_ticks = 0;

static inline uint8_t get_port(uint8_t channel) {
  switch (channel) {
    case PIT_CHANNEL_0:
      return PIT0_DATA;
    case PIT_CHANNEL_1:
      return PIT1_DATA;
    case PIT_CHANNEL_2:
      return PIT2_DATA;
    default:
      return -1;
  }
}

//

void pit_timer_start() {
  uint8_t val = (inb(PIT2_GATE) | 1) & ~0x2;
  outb(PIT2_GATE, val);
}

void pit_timer_stop() {
  uint8_t val = inb(PIT2_GATE) & ~1;
  outb(PIT2_GATE, val);
}

void pit_set_counter(uint8_t channel, uint16_t count) {
  kassert(channel < 3);

  uint16_t port = get_port(channel);
  outb(port, count & 0xFF);
  outb(port, (count >> 8) & 0xFF);
}

//

void pit_interrupt_handler() {
  kprintf("tick %d\n", timer_ticks);
  timer_ticks++;
}

//


void pit_init() {
  idt_hook(VECTOR_TIMER_IRQ, pit_interrupt_handler);
}

void pit_mdelay(uint64_t ms) {
  uint8_t val = pit_control(0, PIT_MODE_0, PIT_ACCESS_WORD, PIT_CHANNEL_2);
  outb(PIT_CONTROL, val);

  while (ms > 0) {
    pit_set_counter(PIT_CHANNEL_2, PIT_CLOCK_RATE / 1000);
    while ((inb(PIT2_GATE) & (1 << 5)) == 0) {
      cpu_pause();
    }
    ms--;
  }
}

void pit_oneshot(uint16_t ms) {
  uint8_t val = pit_control(0, PIT_MODE_0, PIT_ACCESS_WORD, PIT_CHANNEL_0);
  outb(PIT_CONTROL, val);
  pit_set_counter(PIT_CHANNEL_0, ms);
}
