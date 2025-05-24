//
// Created by Aaron Gill-Braun on 2022-06-29.
//

#ifndef KERNEL_INTERRUPT_H
#define KERNEL_INTERRUPT_H

#include <kernel/base.h>

struct trapframe;
struct pci_device;

#define MAX_IRQ 223

typedef void (*irq_handler_t)(struct trapframe *frame);

void irq_init();
int irq_get_vector(uint8_t irq);

int irq_alloc_hardware_irqnum();
int irq_alloc_software_irqnum();
int irq_try_reserve_irqnum(uint8_t irq);
int irq_must_reserve_irqnum(uint8_t irq);

int irq_register_handler(uint8_t irq, irq_handler_t handler, void *data);
int irq_enable_interrupt(uint8_t irq);
int irq_disable_interrupt(uint8_t irq);
int irq_enable_msi_interrupt(uint8_t irq, uint8_t index, struct pci_device *device);
int irq_disable_msi_interrupt(uint8_t irq, uint8_t index, struct pci_device *device);

int early_irq_override_isa_interrupt(uint8_t isa_irq, uint8_t dest_irq, uint16_t flags);

#endif
