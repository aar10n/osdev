//
// Created by Aaron Gill-Braun on 2023-12-29.
//

#ifndef KERNEL_TIME_H
#define KERNEL_TIME_H

#include <kernel/base.h>

#include <abi/time.h>

static inline uint64_t tm2posix(struct tm *tm) {
  uint64_t epoch = 0;
  epoch += tm->tm_sec;
  epoch += (uint64_t)tm->tm_min * 60;
  epoch += (uint64_t)tm->tm_hour * 3600;
  epoch += (uint64_t)tm->tm_yday * 86400;
  epoch += (uint64_t)(tm->tm_year - 70) * 31536000;
  epoch += (uint64_t)((tm->tm_year - 69) / 4) * 86400;
  epoch -= (uint64_t)((tm->tm_year - 1) / 100) * 86400;
  epoch += (uint64_t)((tm->tm_year + 299) / 400) * 86400;
  return epoch;
}

static inline void posix2tm(uint64_t epoch, struct tm *tm) {
  uint64_t days = epoch / 86400;
  uint64_t rem = epoch % 86400;

  uint64_t year = 1970;
  uint64_t leap_years = 0;
  while (days >= 365) {
    if (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) {
      if (days >= 366) {
        days -= 366;
        leap_years++;
      } else {
        break;
      }
    } else {
      days -= 365;
    }
    year++;
  }

  uint64_t month = 0;
  uint64_t month_days = 0;
  while (days >= 28) {
    if (month == 1) {
      if (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) {
        if (days >= 29) {
          days -= 29;
          month_days += 29;
        } else {
          break;
        }
      } else {
        if (days >= 28) {
          days -= 28;
          month_days += 28;
        } else {
          break;
        }
      }
    } else if (month == 3 || month == 5 || month == 8 || month == 10) {
      if (days >= 30) {
        days -= 30;
        month_days += 30;
      } else {
        break;
      }
    } else {
      if (days >= 31) {
        days -= 31;
        month_days += 31;
      } else {
        break;
      }
    }
    month++;
  }

  tm->tm_sec = (int)(rem % 60);
  tm->tm_min = (int)((rem / 60) % 60);
  tm->tm_hour = (int)((rem / 3600) % 24);
  tm->tm_yday = (int)days;
  tm->tm_year = (int)(year - 1900);
  tm->tm_mon = (int)month;
  tm->tm_mday = (int)(days - month_days + 1);
  tm->tm_wday = (int)((epoch / 86400 + 4) % 7);
  tm->tm_isdst = 0;
  tm->tm_gmtoff = 0;
  tm->tm_zone = "UTC";
}

#endif
