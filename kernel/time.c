//
// Created by Aaron Gill-Braun on 2020-09-05.
//

#include <kernel/time.h>

const char *month_str[] = {
  "January", "February", "March", "April", "May", "June",
  "July", "August", "September", "October", "November",
  "December",
};

const char *month_str_alt[] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug",
  "Sep", "Oct", "Nov", "Dec"
};

const char *weekday_str[] = {
  "Monday", "Tuesday", "Wednesday", "Thursday", "Friday",
  "Saturday", "Sunday"
};

const char *weekday_str_alt[] = {
  "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"
};

// sum of days for previous month
uint32_t em[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };

//

int is_leapyear(uint32_t year) {
  if (year % 100 == 0) {
    return year % 400 == 0;
  }
  return year % 4 == 0;
}

uint8_t get_weekday(uint8_t day, uint8_t month, uint32_t year) {
  if (month < 3) {
    year -= 1;
  }

  uint32_t c = (uint32_t) (year / 100);
  uint32_t g = year % 100;
  uint32_t f = 5 * (c % 4);
  uint32_t e = em[month - 1];

  if (month > 2) {
    e -= 1;
  }

  return (day + e + f + g + (g / 4)) % 7;
}

const char *get_weekday_str(uint8_t weekday, bool alt) {
  return alt ? weekday_str_alt[weekday - 1] : weekday_str[weekday -1];
}

const char *get_month_str(uint8_t month, bool alt) {
  return alt ? month_str_alt[month - 1] : month_str[month - 1];
}
