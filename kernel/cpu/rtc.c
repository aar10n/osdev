//
// Created by Aaron Gill-Braun on 2020-08-24.
//

#include <stdbool.h>
#include <stdio.h>

#include <kernel/cpu/asm.h>
#include <kernel/cpu/cpu.h>
#include <kernel/cpu/interrupt.h>
#include <kernel/cpu/rtc.h>
#include <kernel/time.h>

rtc_mode_t curr_mode = -1;
volatile uint32_t time = 0;

uint8_t rtc_pending_update() {
  outb(CMOS_CONFIG_PORT, RTC_REG_STATUS_A);
  return inb(CMOS_DATA_PORT) & 0x80;
}

uint8_t rtc_read(uint8_t reg) {
  outb(CMOS_CONFIG_PORT, reg);
  return inb(CMOS_DATA_PORT);
}

void rtc_write(uint8_t reg, uint8_t data) {
  outb(CMOS_CONFIG_PORT, reg);
  outb(CMOS_DATA_PORT, data);
}

//

void rtc_irq_handler(registers_t regs) {
  time++;

  // If we dont read the value of the C status register
  // the next RTC interrupt will not fire
  outb(CMOS_CONFIG_PORT, RTC_REG_STATUS_C);
  inb(CMOS_DATA_PORT);
}


void rtc_init(rtc_mode_t mode) {
  if (mode == curr_mode) {
    return;
  }

  disable_interrupts();

  if (mode == RTC_MODE_CLOCK) {
    if (curr_mode == RTC_MODE_TIMER) {
      kprintf("unregistering isr\n");
      unregister_isr(IRQ8);
    }

    // ensure default rate and stage divider are selected
    uint8_t reg_a = 0b00100110;
    rtc_write(RTC_REG_STATUS_A, reg_a);

    // configure clock mode
    // enable daylight savings, 24 hour mode and binary output
    uint8_t reg_b = 0b00000111;
    rtc_write(RTC_REG_STATUS_B, reg_b);
  } else if (mode == RTC_MODE_TIMER) {
    // select a timer frequency of 1024 Hz
    uint8_t reg_a = 0b00100110;
    // uint8_t reg_a = 0b00101111;
    rtc_write(RTC_REG_STATUS_A, reg_a);

    // configure timer
    uint8_t reg_b = 0b01000000;
    rtc_write(RTC_REG_STATUS_B, reg_b);

    register_isr(IRQ8, rtc_irq_handler);
    kprintf("starting timer\n");
  }

  curr_mode = mode;
  enable_interrupts();
}

/* ----- Clock mode functions ----- */

void rtc_get_time(rtc_time_t *rtc_time) {
  if (curr_mode != RTC_MODE_CLOCK) {
    kprintf("[rtc] not in clock mode\n");
    return;
  }

  rtc_time->seconds = rtc_read(RTC_REG_SECONDS);
  rtc_time->minutes = rtc_read(RTC_REG_MINUTES);
  rtc_time->hours = rtc_read(RTC_REG_HOURS);

  rtc_time->day = rtc_read(RTC_REG_DAY);
  rtc_time->month = rtc_read(RTC_REG_MONTH);
  rtc_time->year = rtc_read(RTC_REG_YEAR) + 2000;

  rtc_time->weekday = get_weekday(rtc_time->day, rtc_time->month, rtc_time->year);
}

// Debugging

void rtc_print_debug_time(rtc_time_t *rtc_time) {
  // convert to 12-hour rtc_time
  uint8_t hours = rtc_time->hours > 12 ? rtc_time->hours - 12 : rtc_time->hours;
  const char *am_pm = rtc_time->hours > 11 ? "PM" : "AM";

  const char *weekday = get_weekday_str(rtc_time->weekday, false);
  const char *month = get_month_str(rtc_time->month, false);

  kprintf("%s, %s %d, %d\n", weekday, month, rtc_time->day, rtc_time->year);
  kprintf("%d:%02d:%02d %s\n", hours, rtc_time->minutes, rtc_time->seconds, am_pm);
}

void rtc_print_debug_status() {
  uint8_t status_a = rtc_read(RTC_REG_STATUS_A);
  uint8_t status_b = rtc_read(RTC_REG_STATUS_B);
  kprintf("status a: 0b%08b\n", status_a);
  kprintf("status b: 0b%08b\n", status_b);
}

