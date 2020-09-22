//
// Created by Aaron Gill-Braun on 2020-09-20.
//

#ifndef KERNEL_CPU_IOAPIC_H
#define KERNEL_CPU_IOAPIC_H

#include <kernel/cpu/cpu.h>
#include <kernel/cpu/apic.h>

#define get_max_irq(value) \
  ((((value) >> 16) & 0xFF) + 1)

#define make_rdrentry_low(vec, deliv, dest_mode, active_low, trigger, mask) \
  ((vec) | ((deliv) << 8) | ((dest_mode) << 11) | ((active_low) << 13) | \
   ((trigger) << 15) | ((mask) << 16))

#define make_rdrentry_high(dest) \
  ((dest) << 24)

#define rdrentry_index(irq) \
  (0x10 + ((irq) * 2))

#define IOREGSEL 0x00
#define IOREGWIN 0x10

#define IOAPIC_REG_ID      0x00
#define IOAPIC_REG_VERSION 0x01
#define IOAPIC_REG_ARB_ID  0x02
#define IOAPIC_REG_RTB     0x03

void ioapic_init(system_info_t *sysinfo);
void ioapic_set_mask(uint8_t id, uint8_t pin, uint8_t mask);

#endif
