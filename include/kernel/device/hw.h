//
// Created by Aaron Gill-Braun on 2022-06-15.
//

#ifndef KERNEL_DEVICE_HW_H
#define KERNEL_DEVICE_HW_H

#include <base.h>
#include <queue.h>

typedef struct hw_apic_device {
  uint8_t id;
  uint32_t flags;
  uintptr_t base_addr;
  SLIST_ENTRY(struct hw_apic_device) list;
} hw_apic_device_t;

typedef struct hw_hpet_device {
  uint8_t id;
  uint32_t flags;
  uintptr_t base_addr;
  SLIST_ENTRY(struct hw_hpet_device) list;
} hw_hpet_device_t;

typedef struct hw_ioapic_device {

} hw_ioapic_device_t;

#endif
