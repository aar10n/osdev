//
// Created by Aaron Gill-Braun on 2020-08-25.
//

#include <stdint.h>

#include <kernel/cpu/asm.h>
#include <kernel/cpu/idt.h>
#include <kernel/cpu/exception.h>
#include <kernel/cpu/interrupt.h>

// Interrupt Descriptor Table
// IDT entries (called) gates map ISRs to the correct interrupts.

void set_idt_gate(int vector, uint32_t handler) {
  idt[vector].low_offset = low_16(handler);
  idt[vector].selector = KERNEL_CS;
  idt[vector].zero = 0;
  idt[vector].attr.gate_type = INTERRUPT_GATE_32;
  idt[vector].attr.storage_segment = 0;
  idt[vector].attr.privilege_level = 0;
  idt[vector].attr.present = 1;
  idt[vector].high_offset = high_16(handler);
}

void install_idt() {
  // Exception Handlers
  //   Faults - These can be corrected and the program may continue as if nothing happened.
  //   Traps: Traps are reported immediately after the execution of the trapping instruction.
  //   Aborts: Some severe unrecoverable error.
  set_idt_gate(0, (uint32_t) isr0);   // Divide-by-zero Error (Fault)
  set_idt_gate(1, (uint32_t) isr1);   // Debug (Fault/Trap)
  set_idt_gate(2, (uint32_t) isr2);   // Non-maskable Interrupt (Interrupt)
  set_idt_gate(3, (uint32_t) isr3);   // Breakpoint (Trap)
  set_idt_gate(4, (uint32_t) isr4);   // Overflow (Trap)
  set_idt_gate(5, (uint32_t) isr5);   // Bound Range Exceeded (Fault)
  set_idt_gate(6, (uint32_t) isr6);   // Invalid Opcode (Fault)
  set_idt_gate(7, (uint32_t) isr7);   // Device Not Available (Fault)
  set_idt_gate(8, (uint32_t) isr8);   // Double Fault (Abort)
  set_idt_gate(9, (uint32_t) isr9);   // Intel Reserved
  set_idt_gate(10, (uint32_t) isr10); // Invalid TSS (Fault)
  set_idt_gate(11, (uint32_t) isr11); // Segment Not Present (Fault)
  set_idt_gate(12, (uint32_t) isr12); // Stack-Segment Fault (Fault)
  set_idt_gate(13, (uint32_t) isr13); // General Protection (Fault)
  set_idt_gate(14, (uint32_t) isr14); // Page Fault (Fault)
  set_idt_gate(15, (uint32_t) isr15); // Intel Reserved
  set_idt_gate(16, (uint32_t) isr16); // x87 FPU Floating-Point Exception (Fault)
  set_idt_gate(17, (uint32_t) isr17); // Alignment Check (Fault)
  set_idt_gate(18, (uint32_t) isr18); // Machine Check (Abort)
  set_idt_gate(19, (uint32_t) isr19); // SIMD Floating-Point Exception (Fault)
  set_idt_gate(20, (uint32_t) isr20); // Virtualization Exception (Fault)
  set_idt_gate(21, (uint32_t) isr21); // Intel Reserved
  set_idt_gate(22, (uint32_t) isr22); // Intel Reserved
  set_idt_gate(23, (uint32_t) isr23); // Intel Reserved
  set_idt_gate(24, (uint32_t) isr24); // Intel Reserved
  set_idt_gate(25, (uint32_t) isr25); // Intel Reserved
  set_idt_gate(26, (uint32_t) isr26); // Intel Reserved
  set_idt_gate(27, (uint32_t) isr27); // Intel Reserved
  set_idt_gate(28, (uint32_t) isr28); // Intel Reserved
  set_idt_gate(29, (uint32_t) isr29); // Intel Reserved
  set_idt_gate(30, (uint32_t) isr30); // Security Exception
  set_idt_gate(31, (uint32_t) isr31); // Intel Reserved

  // Remap the PIC
  outb(0x20, 0x11);
  outb(0xA0, 0x11);
  outb(0x21, 0x20);
  outb(0xA1, 0x28);
  outb(0x21, 0x04);
  outb(0xA1, 0x02);
  outb(0x21, 0x01);
  outb(0xA1, 0x01);
  outb(0x21, 0x0);
  outb(0xA1, 0x0);

  // Interrupt Handlers
  set_idt_gate(32, (uint32_t) isr32); // Programmable Interrupt Timer Interrupt
  set_idt_gate(33, (uint32_t) isr33); // Keybaord Interrupt
  set_idt_gate(34, (uint32_t) isr34); // Cascade (used internally by two PICs)
  set_idt_gate(35, (uint32_t) isr35); // COM2 (if enabled)
  set_idt_gate(36, (uint32_t) isr36); // COM1 (if enabled)
  set_idt_gate(37, (uint32_t) isr37); // LPT2 (if enabled)
  set_idt_gate(38, (uint32_t) isr38); // Floppy Disk
  set_idt_gate(39, (uint32_t) isr39); // LPT1 / Unreliable
  set_idt_gate(40, (uint32_t) isr40); // CMOS real-time clock
  set_idt_gate(41, (uint32_t) isr41); // Free for peripherals / legacy SCSI / NIC
  set_idt_gate(42, (uint32_t) isr42); // Free for peripherals / SCSI / NIC
  set_idt_gate(43, (uint32_t) isr43); // Free for peripherals / SCSI / NIC
  set_idt_gate(44, (uint32_t) isr44); // PS2 Mouse
  set_idt_gate(45, (uint32_t) isr45); // FPU / Coprocessor / Inter-processor
  set_idt_gate(46, (uint32_t) isr46); // Primary ATA Hard Disk
  set_idt_gate(47, (uint32_t) isr47); // Secondary ATA Hard Disk

  idt_reg.base = (uint32_t) &idt;
  idt_reg.limit = IDT_ENTRIES * sizeof(idt_gate_t) - 1;
  load_idt(&idt_reg);
}
