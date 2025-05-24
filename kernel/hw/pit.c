//
// Created by Aaron Gill-Braun on 2024-01-01.
//

// Intel 8254 Programmable Interval Timer (PIT) driver
#include <kernel/hw/pit.h>
#include <kernel/cpu/cpu.h>
#include <kernel/cpu/io.h>

#include <kernel/alarm.h>
#include <kernel/panic.h>


#define ASSERT(x) kassert(x)
#define DPRINTF(x, ...) kprintf("pit: " x, ##__VA_ARGS__)

#define PIT_CHANNEL_0 0x40
#define PIT_CHANNEL_1 0x41
#define PIT_CHANNEL_2 0x42
#define PIT_CONTROL   0x43
#define PIT_READBACK  0x44
#define CHANNEL2_GATE 0x61

#define GATE2_ENABLE  0x01
#define GATE2_STATUS  0x10

/* bcd/binary mode */
#define PIT_BINARY 0b00
#define PIT_BCD    0b01
/* operating mode */
#define PIT_MODE_0 0b000
#define PIT_MODE_1 0b001
#define PIT_MODE_2 0b010
#define PIT_MODE_3 0b011
#define PIT_MODE_4 0b100
#define PIT_MODE_5 0b101
/* access mode */
#define PIT_ACCESS_LATCH 0b00
#define PIT_ACCESS_LSB   0b01
#define PIT_ACCESS_MSB   0b10
#define PIT_ACCESS_WORD  0b11
/* select channel */
#define PIT_SEL_CHANNEL0  0b00
#define PIT_SEL_CHANNEL1  0b01
#define PIT_SEL_CHANNEL2  0b10
#define PIT_SEL_READBACK  0b11

#define CONTROL_BYTE(bcd, mode, access, channel) \
  ((bcd) | ((mode) << 1) | ((access) << 4) | ((channel) << 6))

#define PIT_FREQUENCY 1193182 // Hz
// 1000000000 / 1193182 = 838


static inline uint16_t read_counter(uint8_t channel) {
  uint16_t port = PIT_CHANNEL_0 + channel;
  return inb(port) | (inb(port) << 8);
}

static inline void write_counter(uint8_t channel, uint16_t count) {
  uint16_t port = PIT_CHANNEL_0 + channel;
  outb(port, count & 0xFF);
  outb(port, (count >> 8) & 0xFF);
}

//////////////////////////////
// MARK: PIT alarm source

static int pit_alarm_source_init(alarm_source_t *as, uint32_t mode, irq_handler_t handler) {
  as->mode = mode;
  as->irq_num = irq_must_reserve_irqnum(0);
  if (mode == ALARM_CAP_ONE_SHOT) {
    // set counter 0 to mode 0
    outb(PIT_CONTROL, CONTROL_BYTE(PIT_BINARY, PIT_MODE_0, PIT_ACCESS_WORD, PIT_SEL_CHANNEL0));
    write_counter(PIT_CHANNEL_0, 0);
    irq_register_handler(as->irq_num, handler, as);
  } else if (mode == ALARM_CAP_PERIODIC) {
    // set counter 0 to mode 2
    outb(PIT_CONTROL, CONTROL_BYTE(PIT_BINARY, PIT_MODE_2, PIT_ACCESS_WORD, PIT_SEL_CHANNEL0));
    write_counter(PIT_CHANNEL_0, 0);
    irq_register_handler(as->irq_num, handler, as);
  } else {
    DPRINTF("invalid alarm mode\n");
    return -1;
  }

  return 0;
}

static int pit_alarm_source_enable(alarm_source_t *as) {
  irq_enable_interrupt(as->irq_num);
  return 0;
}

static int pit_alarm_source_disable(alarm_source_t *as) {
  irq_disable_interrupt(as->irq_num);
  return 0;
}

static int pit_alarm_source_setval(alarm_source_t *as, uint64_t value) {
  DPRINTF("setval: %llu\n", value);
  uint64_t flags = cpu_save_clear_interrupts();
  if (as->mode == ALARM_CAP_ONE_SHOT) {
    // set counter 0 to mode 0
    outb(PIT_CONTROL, CONTROL_BYTE(PIT_BINARY, PIT_MODE_0, PIT_ACCESS_WORD, PIT_SEL_CHANNEL0));
    write_counter(PIT_CHANNEL_0, value);
  } else if (as->mode == ALARM_CAP_PERIODIC) {
    // set counter 0 to mode 2
    outb(PIT_CONTROL, CONTROL_BYTE(PIT_BINARY, PIT_MODE_2, PIT_ACCESS_WORD, PIT_SEL_CHANNEL0));
    write_counter(PIT_CHANNEL_0, value);
  } else {
    DPRINTF("invalid alarm mode\n");
    cpu_restore_interrupts(flags);
    return -1;
  }

  cpu_restore_interrupts(flags);
  return 0;
}


static alarm_source_t pit_alarm_source = {
  .name = "pit",
  .cap_flags = ALARM_CAP_ONE_SHOT | ALARM_CAP_PERIODIC,
  .scale_ns = NS_PER_SEC / PIT_FREQUENCY,
  .value_mask = 0xFFFF,
  .init = pit_alarm_source_init,
  .enable = pit_alarm_source_enable,
  .disable = pit_alarm_source_disable,
  .setval = pit_alarm_source_setval,
};

static void pit_early_init() {
  register_alarm_source(&pit_alarm_source);
}
EARLY_INIT(pit_early_init);

//

void pit_mdelay(uint32_t ms) {
  // set counter 2 to mode 4, LSB only
  outb(PIT_CONTROL, CONTROL_BYTE(PIT_BINARY, PIT_MODE_4, PIT_ACCESS_LSB, PIT_SEL_CHANNEL2));
  while (ms) {
    uint16_t count = PIT_FREQUENCY / 1000; // 1ms
    write_counter(PIT_CHANNEL_2, count);
    while (!(inb(CHANNEL2_GATE) & GATE2_STATUS));
    ms--;
  }
}
