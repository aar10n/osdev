//
// Created by Aaron Gill-Braun on 2020-08-31.
//

#include <stdint.h>
#include <stdio.h>
#include <math.h>

typedef union double_raw {
  double value;
  struct {
    uint64_t frac : 52;
    uint64_t exp : 11;
    uint64_t sign : 1;
  };
  struct {
    uint32_t frac_low : 32;
    uint32_t frac_high : 20;
    uint32_t expr : 11;
    uint32_t sign : 1;
  } alt;
} double_raw_t;



void printf_fp(double value) {
  double_raw_t raw = { .value = value };

  kprintf("%b | %011b | %020b%032b\n", raw.sign, raw.exp, raw.alt.frac_high, raw.alt.frac_low);

  if (raw.exp == 0 && raw.frac == 0) {
    // signed zero
    kprintf("signed zero\n");
    return;
  } else if (raw.exp == 0x7FF && raw.frac == 0) {
    // infinity
    kprintf("infinity\n");
    return;
  } else if (raw.exp == 0x7FF && raw.frac != 0) {
    // NaN
    kprintf("NaN\n");
    return;
  }
}
