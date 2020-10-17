//
// Created by Aaron Gill-Braun on 2020-10-17.
//

#ifndef KERNEL_SYSTEM_H
#define KERNEL_SYSTEM_H

#include <base.h>

//
// System Information and Supported Hardware
//

// Local Apic Descriptor

typedef struct {
  uint8_t id;
  union {
    uint8_t raw;
    struct {
      uint8_t enabled : 1;
      uint8_t bsp : 1;
      uint8_t reserved : 6;
    };
  } flags;
} apic_desc_t;

// logical core
typedef struct {
  uint8_t id;
  apic_desc_t *local_apic;
} core_desc_t;

// I/O Apic Descriptor

typedef struct irq_source {
  uint8_t source_irq;
  uint8_t dest_int;
  uint8_t flags;
  struct irq_source *next;
} irq_source_t;

typedef struct {
  uint8_t id;
  uint8_t version;
  uint8_t max_rentry;
  uint8_t int_base;
  uintptr_t phys_addr;
  uintptr_t virt_addr;
  irq_source_t *sources;
} ioapic_desc_t;

// HPET Descriptor

typedef struct {
  union {
    uint32_t raw;
    struct {
      uint32_t hw_rev_id : 8;
      uint32_t comp_count : 5;
      uint32_t counter_size : 1;
      uint32_t : 1;
      uint32_t legacy_irq_routing : 1;
      uint32_t pci_vendor_id : 16;
    };
  } block_id;
  uint8_t number;
  uintptr_t phys_addr;
  uintptr_t virt_addr;
} hpet_desc_t;

// System Information

typedef struct {
  uintptr_t apic_phys_addr;
  uintptr_t apic_virt_addr;
  uint8_t bsp_id;
  // logical cores
  uint8_t core_count;
  core_desc_t *cores;
  // ioapics
  uint8_t ioapic_count;
  ioapic_desc_t *ioapics;
  // hpet
  hpet_desc_t *hpet;
} system_info_t;

extern system_info_t *system_info;

#endif
