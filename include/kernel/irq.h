//
// Created by Aaron Gill-Braun on 2022-06-29.
//

#ifndef KERNEL_INTERRUPT_H
#define KERNEL_INTERRUPT_H

#include <base.h>
#include <cpu/cpu.h>


typedef struct pcie_device pcie_device_t;
typedef void (*irq_handler_t)(uint8_t, void *);
typedef void (*exception_handler_t)(uint8_t, uint32_t, cpu_irq_stack_t *, cpu_registers_t *);

void irq_early_init();
void irq_init();

int irq_alloc_hardware_irqnum();
int irq_alloc_software_irqnum();
void irq_reserve_irqnum(uint8_t irq);

int irq_register_exception_handler(uint8_t vector, exception_handler_t handler);
int irq_register_irq_handler(uint8_t irq, irq_handler_t handler, void *data);

int irq_enable_interrupt(uint8_t irq);
int irq_disable_interrupt(uint8_t irq);
int irq_enable_msi_interrupt(uint8_t irq, uint8_t index, pcie_device_t *device);
int irq_disable_msi_interrupt(uint8_t irq, uint8_t index, pcie_device_t *device);

int irq_override_isa_interrupt(uint8_t isa_irq, uint8_t dest_irq, uint16_t flags);

#endif
