//
// Created by Aaron Gill-Braun on 2020-10-14.
//

#ifndef KERNEL_DEVICE_IOAPIC_H
#define KERNEL_DEVICE_IOAPIC_H

#include <kernel/base.h>
#include <kernel/cpu/cpu.h>

int ioapic_get_max_remappable_irq();

int ioapic_set_isa_irq_routing(uint8_t isa_irq, uint8_t dest_irq, uint16_t flags);
int ioapic_set_irq_vector(uint8_t irq, uint8_t vector);
int ioapic_set_irq_dest(uint8_t irq, uint8_t mode, uint8_t dest);
int ioapic_set_irq_mask(uint8_t irq, bool mask);

void register_ioapic(uint8_t id, uintptr_t address, uint32_t gsi_base);
void disable_legacy_pic();

#endif
