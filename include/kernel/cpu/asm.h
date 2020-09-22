//
// Created by Aaron Gill-Braun on 2019-04-21.
//

#ifndef KERNEL_CPU_ASM_H
#define KERNEL_CPU_ASM_H

#include <stdint.h>
#include <kernel/cpu/cpu.h>

void cpuinfo(cpuinfo_t *info);
int has_long_mode();

uintptr_t get_eip();
uintptr_t get_esp();
uintptr_t get_ebp();

void get_msr(uint32_t msr, uint64_t *result);
void set_msr(uint32_t msr, uint64_t value);

void outb(uint16_t port, uint8_t data);
uint8_t inb(uint16_t port);
void outw(uint16_t port, uint16_t data);
uint16_t inw(uint16_t port);
void outdw(uint16_t port, uint32_t data);
uint32_t indw(uint16_t port);

void load_idt(void *idt);
void load_gdt(void *idt);

void enable_interrupts();
void disable_interrupts();

int has_fpu();
int has_sse();
void enable_sse();

#endif // KERNEL_CPU_ASM_H
