//
// Created by Aaron Gill-Braun on 2020-08-24.
//

#ifndef KERNEL_CPU_RTC_H
#define KERNEL_CPU_RTC_H

#include <stdint.h>

#define CMOS_CONFIG_PORT 0x70
#define CMOS_DATA_PORT 0x71

#define CMOS_DRIVE_REGISTER 0x10

// CMOS RTC Registers
#define RTC_REG_SECONDS 0x00
#define RTC_REG_MINUTES 0x02
#define RTC_REG_HOURS   0x04
#define RTC_REG_DAY     0x07
#define RTC_REG_MONTH   0x08
#define RTC_REG_YEAR    0x09

#define RTC_REG_STATUS_A 0x0A
#define RTC_REG_STATUS_B 0x0B
#define RTC_REG_STATUS_C 0x0C

typedef struct {
  uint8_t seconds;
  uint8_t minutes;
  uint8_t hours;

  uint8_t weekday;
  uint8_t day;
  uint8_t month;
  uint16_t year;
} rtc_time_t;

typedef enum {
  RTC_MODE_CLOCK,
  RTC_MODE_TIMER
} rtc_mode_t;

void rtc_init(rtc_mode_t mode);

void rtc_get_time(rtc_time_t *rtc_time);

void rtc_print_debug_time(rtc_time_t *rtc_time);
void rtc_print_debug_status();

#endif
