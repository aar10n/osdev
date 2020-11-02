//
// Created by Aaron Gill-Braun on 2020-10-14.
//

#ifndef KERNEL_VECTORS_H
#define KERNEL_VECTORS_H

// Priority 0xF - Highest priority
#define VECTOR_APIC_SPURIOUS 0xFF
#define VECTOR_IPI_CPU_HLT   0xF2
#define VECTOR_IPI_TEST      0xF0

// Priority 0x4 - Timer vectors
#define VECTOR_SCHED_TIMER   0x45
#define VECTOR_HPET_TIMER    0x44
#define VECTOR_APIC_LINT1    0x43
#define VECTOR_APIC_LINT0    0x42
#define VECTOR_APIC_PERFC    0x41
#define VECTOR_APIC_THERMAL  0x40

// Priority 0x3 - External interrupts
#define VECTOR_AHCI_IRQ      0x31
#define VECTOR_KEYBOARD_IRQ  0x30

// Priority 0x2 - Lowest priorty
#define VECTOR_PIC_IRQ15     0x2F
#define VECTOR_PIC_IRQ8      0x28
#define VECTOR_PIC_IRQ7      0x27
#define VECTOR_PIC_IRQ0      0x20

#endif
