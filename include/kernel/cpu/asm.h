//
// Created by Aaron Gill-Braun on 2019-04-21.
//

#ifndef KERNEL_CPU_ASM_H
#define KERNEL_CPU_ASM_H

#include <stdint.h>

void outb(uint16_t port, uint8_t data);
uint8_t inb(uint16_t port);
void outw(uint16_t port, uint16_t data);
uint16_t inw(uint16_t port);
void outdw(uint16_t port, uint32_t data);
uint32_t indw(uint16_t port);

void load_idt(void *idt);
void load_gdt(void *idt);

void invl_page(uint32_t page);

void interrupt();
void interrupt_out_of_memory();
void enable_interrupts();
void disable_interrupts();
int has_fpu();

#endif // KERNEL_CPU_ASM_H
