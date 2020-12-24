//
// Created by Aaron Gill-Braun on 2020-08-25.
//

#ifndef KERNEL_CPU_CPU_H
#define KERNEL_CPU_CPU_H

#include <base.h>

void cli();
void sti();

uint64_t cli_save();
void sti_restore(uint64_t rflags);

uint64_t read_tsc();

uint64_t read_msr(uint32_t msr);
void write_msr(uint32_t msr, uint64_t value);

uint64_t read_fsbase();
void write_fsbase(uint64_t value);
uint64_t read_gsbase();
void write_gsbase(uint64_t value);
uint64_t read_kernel_gsbase();
void write_kernel_gsbase(uint64_t value);
void swapgs();

void load_gdt(void *gdtr);
void load_idt(void *idtr);
void load_tr(uint16_t tss);
void flush_gdt();

uint64_t read_cr0();
void write_cr0(uint64_t cr0);
uint64_t read_cr3();
void write_cr3(uint64_t cr3);
uint64_t read_cr4();
void write_cr4(uint64_t cr4);

void tlb_invlpg(uint64_t addr);
void tlb_flush();

void enable_sse();

int syscall(int call);
void sysret(uintptr_t rip, uintptr_t rsp);

#endif
