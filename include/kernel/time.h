//
// Created by Aaron Gill-Braun on 2020-09-05.
//

#ifndef KERNEL_TIME_H
#define KERNEL_TIME_H

#include <stdbool.h>
#include <stdint.h>

int is_leapyear(uint32_t year);
uint8_t get_weekday(uint8_t day, uint8_t month, uint32_t year);
const char *get_weekday_str(uint8_t weekday, bool alt);
const char *get_month_str(uint8_t month, bool alt);

void usleep(uint32_t microseconds);

#endif
