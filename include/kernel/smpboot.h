//
// Created by Aaron Gill-Braun on 2020-10-25.
//

#ifndef KERNEL_SMPBOOT_H
#define KERNEL_SMPBOOT_H

#include <base.h>

#define AP_SUCCESS 1

typedef struct {
  uint8_t status;        // 0x00 - ap boot status
  uint8_t : 8;           // 0x01 - reserved
  uint16_t : 16;         // 0x02 - reserved
  uint32_t pml4_addr;    // 0x04 - physical pml4 address
  uintptr_t stack_addr;  // 0x08 - virtual stack address
} smp_data_t;

static_assert(sizeof(smp_data_t) == (sizeof(uint64_t) * 2));

void smp_init();

#endif
