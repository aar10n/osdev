//
// Created by Aaron Gill-Braun on 2020-10-15.
//

#ifndef KERNEL_DEVICE_PIT_H
#define KERNEL_DEVICE_PIT_H

#include <base.h>

#define PIT_CLOCK_RATE 1193182UL

#define PIT_CHANNEL_0  0x0
#define PIT_CHANNEL_1  0x1
#define PIT_CHANNEL_2  0x2
#define PIT_CHANNEL_RB 0x3

#define PIT_ACCESS_LATCH  0x0
#define PIT_ACCESS_LOBYTE 0x1
#define PIT_ACCESS_HIBYTE 0x2
#define PIT_ACCESS_WORD   0x3

#define PIT_MODE_0 0x0 // interrupt on terminal count
#define PIT_MODE_1 0x1 // hardware re-triggerable one-shot
#define PIT_MODE_2 0x2 // rate generator
#define PIT_MODE_3 0x3 // square wave generator
#define PIT_MODE_4 0x4 // software triggered strobe
#define PIT_MODE_5 0x5 // hardware triggered strobe
#define PIT_MODE_6 0x2 // same as mode 2
#define PIT_MODE_7 0x3 // same as mode 3

void pit_timer_start();
void pit_timer_stop();
void pit_set_counter(uint8_t channel, uint16_t count);

void pit_mdelay(uint64_t ms);

#endif
