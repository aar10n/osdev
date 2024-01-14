//
// Created by Aaron Gill-Braun on 2023-12-29.
//

#ifndef KERNEL_DEVICE_RTC_H
#define KERNEL_DEVICE_RTC_H

#include <kernel/base.h>

struct rtc_time {
  uint8_t second;
  uint8_t minute;
  uint8_t hour;
  uint8_t day;
  uint8_t weekday;
  uint8_t month;
  uint8_t year;
};

void rtc_get_time(struct rtc_time *time);

#endif
