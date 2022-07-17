//
// Created by Aaron Gill-Braun on 2020-10-25.
//

#ifndef KERNEL_SMPBOOT_H
#define KERNEL_SMPBOOT_H

#include <base.h>

typedef volatile struct packed smp_data {
  uint16_t lock;              // 0x00 - ap spinlock
  uint16_t gate;              // 0x02 - ap gate
  uint16_t current_id;        // 0x04 - current cpu id
  uint16_t count;             // 0x06 - ap count
  uint64_t pml4_addr;         // 0x08 - pml4
  uint64_t stack_addr;        // 0x10 - stack top pointer
  uint64_t percpu_ptr;        // 0x18 - percpu data area
} smp_data_t;
static_assert(sizeof(smp_data_t) == (sizeof(uint64_t) * 4));
static_assert(offsetof(smp_data_t, pml4_addr) == 0x8);
static_assert(offsetof(smp_data_t, stack_addr) == 0x10);
// char (*__kaboom)[sizeof(smp_data_t)] = 1;

void smp_init();

#endif
