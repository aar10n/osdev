//
// Created by Aaron Gill-Braun on 2020-10-17.
//

#ifndef KERNEL_DEVICE_HPET_H
#define KERNEL_DEVICE_HPET_H

#include <base.h>

// Registers

typedef union {
  uint64_t raw;
  struct {
    uint64_t rev_id : 8;         // revision id
    uint64_t timer_count : 5;    // number of timers
    uint64_t count_size : 1;     // 0 = 32-bit | 1 = 64-bit
    uint64_t : 1;                // reserved
    uint64_t legacy_replace : 1; // legacy replacement is supported
    uint64_t vendor_id : 16;     // PCI vendor id
    uint64_t clock_period : 32;  // clock period in femtoseconds
  };
} hpet_reg_id_t;

typedef union {
  uint64_t raw;
  struct {
    uint64_t enabled : 1;        // overall enable
    uint64_t legacy_replace : 1; // enables legacy replacement
    uint64_t : 62;               // reserved
  };
} hpet_reg_config_t;
#define hpet_reg_config(en, leg) \
  ((hpet_reg_config_t){ .enabled = en, .legacy_mapping = leg })

typedef union {
  uint64_t raw;
  struct {
    uint64_t : 1;
    uint64_t trigger_mode : 1; // 0 = edge | 1 = level
    uint64_t int_enabled : 1;  // interrupt generation enabled
    uint64_t timer_mode : 1;   // 0 = non-periodic | 1 = periodic
    uint64_t periodic_cap : 1; // period mode supported
    uint64_t reg_size : 1;     // 0 = 32-bit | 1 = 64-bit register
    uint64_t set_value : 1;    // allows timer value to be set directly
    uint64_t : 1;              // reserved
    uint64_t force_32bit : 1;  // forces 64-bit timer to run in 32-bit
    uint64_t int_route : 5;    // ioapic interrupt routing
    uint64_t fsb_enabled : 1;  // uses fsb interrupt mapping
    uint64_t fsb_cap : 1;      // timer supports fsb interrupt mapping
    uint64_t : 16;             // reserved
    uint64_t routing_cap : 32; // interrupt routing capabilities
  };
} hpet_reg_timer_config_t;
#define hpet_reg_timer_config(trg, en, tm, sv, f32, rt, fsb) \
  ((hpet_reg_timer_config_t){                                \
    .trigger_mode = trg, .int_enabled = en, .timer_mode = tm, .set_value = sv, \
    .force_32bit = f32, .int_route = rt, .fsb_enabled = fsb \
  })

void register_hpet(uint8_t id, uintptr_t address, uint16_t min_period);

uint64_t hpet_get_count();
uint32_t hpet_get_scale_ns();

#endif
