//
// Created by Aaron Gill-Braun on 2020-08-25.
//

#ifndef KERNEL_CPU_EXCEPTION_H
#define KERNEL_CPU_EXCEPTION_H

#include <stdint.h>
#include <kernel/cpu/cpu.h>

#define EXC0  0
#define EXC1  1
#define EXC2  2
#define EXC3  3
#define EXC4  4
#define EXC5  5
#define EXC6  6
#define EXC7  7
#define EXC8  8
#define EXC9  9
#define EXC10 10
#define EXC11 11
#define EXC12 12
#define EXC13 13
#define EXC14 14
#define EXC15 15
#define EXC16 16
#define EXC17 17
#define EXC18 18
#define EXC19 19
#define EXC20 20
#define EXC21 21
#define EXC22 22
#define EXC23 23
#define EXC24 24
#define EXC25 25
#define EXC26 26
#define EXC27 27
#define EXC28 28
#define EXC29 29
#define EXC30 30
#define EXC31 31

// Exceptions
extern void isr0();
extern void isr1();
extern void isr2();
extern void isr3();
extern void isr4();
extern void isr5();
extern void isr6();
extern void isr7();
extern void isr8();
extern void isr9();
extern void isr10();
extern void isr11();
extern void isr12();
extern void isr13();
extern void isr14();
extern void isr15();
extern void isr16();
extern void isr17();
extern void isr18();
extern void isr19();
extern void isr20();
extern void isr21();
extern void isr22();
extern void isr23();
extern void isr24();
extern void isr25();
extern void isr26();
extern void isr27();
extern void isr28();
extern void isr29();
extern void isr30();
extern void isr31();

_Noreturn void exception_handler(cpu_t cpu, uint32_t int_no, uint32_t err_code);

#endif
