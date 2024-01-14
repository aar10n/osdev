//
// Created by Aaron Gill-Braun on 2023-12-29.
//

#include <kernel/hw/rtc.h>
#include <kernel/cpu/io.h>

#define RTC_SECONDS 0x00
#define RTC_MINUTES 0x02
#define RTC_HOURS   0x04
#define RTC_WEEKDAY 0x06
#define RTC_DAY     0x07
#define RTC_MONTH   0x08
#define RTC_YEAR    0x09
#define RTC_CENTURY 0x32

#define RTC_STATUS_A 0x0A
#define RTC_STATUS_B 0x0B
#define RTC_STATUS_C 0x0C
#define RTC_STATUS_D 0x0D

#define RTC_REG_A_UIP   0x80 // update in progress

#define RTC_REG_B_24H   0x02 // 0 = 12-hour, 1 = 24-hour
#define RTC_REG_B_BIN   0x04 // 0 = BCD, 1 = binary

static inline uint8_t rtc_read(uint8_t reg) {
  outb(0x70, reg);
  return inb(0x71);
}

static inline void rtc_write(uint8_t reg, uint8_t val) {
  outb(0x70, reg);
  outb(0x71, val);
}

static inline uint8_t rtc_bcd_to_bin(uint8_t bcd) {
  return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

static inline uint8_t rtc_bin_to_bcd(uint8_t bin) {
  return ((bin / 10) << 4) | (bin % 10);
}

static inline uint8_t rtc_get_status() {
  return rtc_read(RTC_STATUS_C);
}

static inline void rtc_wait_update() {
  while (rtc_read(RTC_STATUS_A) & RTC_REG_A_UIP);
}


static void rtc_early_init() {
  rtc_wait_update();
  rtc_write(RTC_STATUS_B, RTC_REG_B_24H | RTC_REG_B_BIN);
}
EARLY_INIT(rtc_early_init);


void rtc_get_time(struct rtc_time *time) {
  rtc_wait_update();

  time->second = rtc_read(RTC_SECONDS);
  time->minute = rtc_read(RTC_MINUTES);
  time->hour = rtc_read(RTC_HOURS);
  time->day = rtc_read(RTC_DAY);
  time->weekday = rtc_read(RTC_WEEKDAY);
  time->month = rtc_read(RTC_MONTH);
  time->year = rtc_read(RTC_YEAR);

  if (!(rtc_read(RTC_STATUS_B) & RTC_REG_B_BIN)) {
    time->second = rtc_bcd_to_bin(time->second);
    time->minute = rtc_bcd_to_bin(time->minute);
    time->hour = rtc_bcd_to_bin(time->hour);
    time->day = rtc_bcd_to_bin(time->day);
    time->weekday = rtc_bcd_to_bin(time->weekday);
    time->month = rtc_bcd_to_bin(time->month);
    time->year = rtc_bcd_to_bin(time->year);
  }
}
