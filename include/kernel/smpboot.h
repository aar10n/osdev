//
// Created by Aaron Gill-Braun on 2020-10-25.
//

#ifndef KERNEL_SMPBOOT_H
#define KERNEL_SMPBOOT_H

#include <kernel/base.h>
#include <kernel/cpu/cpu.h>

volatile struct packed smp_data {
  volatile uint32_t init_id;       // 0x00 - id of the apic allowed to boot
  volatile uint32_t gate;          // 0x04 - bsp gate
  volatile uint32_t ap_ack;        // 0x08 - allowed AP acknowledge
  volatile uint32_t ack_bitmap[MAX_CPUS/32]; // 0x0C - bitmap of booted apic ids
  // fields valid during for each AP during boot
  uint64_t pml4_addr _aligned(8); // 0x10 - physical ap pml4 pointer
  uint64_t stack_addr;            // 0x18 - ap stack top pointer
  uint64_t percpu_ptr;            // 0x20 - percpu data area
  uint64_t maintd_ptr;            // 0x28 - pointer to main thread allocated for the initial cpu context
  uint64_t idletd_ptr;            // 0x30 - pointer to idle thread allocated for the initial cpu context
  uint64_t space_ptr;             // 0x38 - pointer to address space allocated for the cpu
};
static_assert(sizeof(struct smp_data) <= PAGE_SIZE);
static_assert(offsetof(struct smp_data, init_id) == 0x00);
static_assert(offsetof(struct smp_data, gate) == 0x04);
static_assert(offsetof(struct smp_data, ap_ack) == 0x08);
static_assert(offsetof(struct smp_data, ack_bitmap) == 0x0c);
static_assert(offsetof(struct smp_data, pml4_addr) == 0x10);
static_assert(offsetof(struct smp_data, stack_addr) == 0x18);
static_assert(offsetof(struct smp_data, percpu_ptr) == 0x20);
static_assert(offsetof(struct smp_data, maintd_ptr) == 0x28);
static_assert(offsetof(struct smp_data, idletd_ptr) == 0x30);
static_assert(offsetof(struct smp_data, space_ptr) == 0x38);

void smp_init();

#endif
