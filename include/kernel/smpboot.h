//
// Created by Aaron Gill-Braun on 2020-10-25.
//

#ifndef KERNEL_SMPBOOT_H
#define KERNEL_SMPBOOT_H

#include <kernel/base.h>

volatile struct packed smp_data {
  uint32_t lock;              // 0x00 - ap spinlock
  uint32_t gate;              // 0x04 - ap gate
  uint32_t current_id;        // 0x08 - current apic id
  uint32_t count;             // 0x0C - ap count
  uint64_t pml4_addr;         // 0x10 - pml4
  uint64_t stack_addr;        // 0x18 - stack top pointer
  uint64_t percpu_ptr;        // 0x20 - percpu data area
};
static_assert(sizeof(struct smp_data) <= PAGE_SIZE);
static_assert(offsetof(struct smp_data, pml4_addr) == 0x10);
static_assert(offsetof(struct smp_data, stack_addr) == 0x18);

void smp_init();

#endif
