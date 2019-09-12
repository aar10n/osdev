//
// Created by Aaron Gill-Braun on 2019-04-18.
//

#include <stdio.h>
#include <stdlib.h>

#include <drivers/asm.h>
#include <drivers/screen.h>

#include <kernel/cpu/idt.h>
#include <kernel/cpu/isr.h>

isr_t interrupt_handlers[256];

void install_isr() {
  set_idt_gate(0, (uint32_t) isr0);
  set_idt_gate(1, (uint32_t) isr1);
  set_idt_gate(2, (uint32_t) isr2);
  set_idt_gate(3, (uint32_t) isr3);
  set_idt_gate(4, (uint32_t) isr4);
  set_idt_gate(5, (uint32_t) isr5);
  set_idt_gate(6, (uint32_t) isr6);
  set_idt_gate(7, (uint32_t) isr7);
  set_idt_gate(8, (uint32_t) isr8);
  set_idt_gate(9, (uint32_t) isr9);
  set_idt_gate(10, (uint32_t) isr10);
  set_idt_gate(11, (uint32_t) isr11);
  set_idt_gate(12, (uint32_t) isr12);
  set_idt_gate(13, (uint32_t) isr13);
  set_idt_gate(14, (uint32_t) isr14);
  set_idt_gate(15, (uint32_t) isr15);
  set_idt_gate(16, (uint32_t) isr16);
  set_idt_gate(17, (uint32_t) isr17);
  set_idt_gate(18, (uint32_t) isr18);
  set_idt_gate(19, (uint32_t) isr19);
  set_idt_gate(20, (uint32_t) isr20);
  set_idt_gate(21, (uint32_t) isr21);
  set_idt_gate(22, (uint32_t) isr22);
  set_idt_gate(23, (uint32_t) isr23);
  set_idt_gate(24, (uint32_t) isr24);
  set_idt_gate(25, (uint32_t) isr25);
  set_idt_gate(26, (uint32_t) isr26);
  set_idt_gate(27, (uint32_t) isr27);
  set_idt_gate(28, (uint32_t) isr28);
  set_idt_gate(29, (uint32_t) isr29);
  set_idt_gate(30, (uint32_t) isr30);
  set_idt_gate(31, (uint32_t) isr31);

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

  // Install the IRQs
  set_idt_gate(32, (uint32_t) irq0);
  set_idt_gate(33, (uint32_t) irq1);
  set_idt_gate(34, (uint32_t) irq2);
  set_idt_gate(35, (uint32_t) irq3);
  set_idt_gate(36, (uint32_t) irq4);
  set_idt_gate(37, (uint32_t) irq5);
  set_idt_gate(38, (uint32_t) irq6);
  set_idt_gate(39, (uint32_t) irq7);
  set_idt_gate(40, (uint32_t) irq8);
  set_idt_gate(41, (uint32_t) irq9);
  set_idt_gate(42, (uint32_t) irq10);
  set_idt_gate(43, (uint32_t) irq11);
  set_idt_gate(44, (uint32_t) irq12);
  set_idt_gate(45, (uint32_t) irq13);
  set_idt_gate(46, (uint32_t) irq14);
  set_idt_gate(47, (uint32_t) irq15);

  install_idt();
}

/* To print the message which defines every exception */
char *exception_messages[] = {
  "Division By Zero",
  "Debug",
  "Non Maskable Interrupt",
  "Breakpoint",
  "Into Detected Overflow",
  "Out of Bounds",
  "Invalid Opcode",
  "No Coprocessor",

  "Double Fault",
  "Coprocessor Segment Overrun",
  "Bad TSS",
  "Segment Not Present",
  "Stack Fault",
  "General Protection Fault",
  "Page Fault",
  "Unknown Interrupt",

  "Coprocessor Fault",
  "Alignment Check",
  "Machine Check",
  "Reserved",
  "Reserved",
  "Reserved",
  "Reserved",
  "Reserved",

  "Reserved",
  "Reserved",
  "Reserved",
  "Reserved",
  "Reserved",
  "Reserved",
  "Reserved",
  "Reserved",
};

void isr_debug_dump(cpu_t cpu, uint32_t int_no, uint32_t err_code) {
  kprintf("-- cpu exception --\n");
  kprintf("interrupt number: %d\n", int_no);
  kprintf("error code: %d\n", err_code);
  kprintf("general registers:\n");
  kprintf("  eax: %p\n", cpu.eax);
  kprintf("  ebx: %p\n", cpu.ebx);
  kprintf("  ecx: %p\n", cpu.ecx);
  kprintf("  edx: %p\n", cpu.edx);
  kprintf("  esi: %p\n", cpu.esi);
  kprintf("  edi: %p\n", cpu.edi);
  kprintf("  esp: %p\n", cpu.esp);
  kprintf("  ebp: %p\n", cpu.ebp);
  kprintf("control registers:\n");
  kprintf("  cr0: %b\n", cpu.cr0);
  kprintf("  cr2: %p\n", cpu.cr2);
  kprintf("  cr3: %p\n", cpu.cr3);
  kprintf("  cr4: %b\n", cpu.cr4);
}

void isr_handler(cpu_t cpu, uint32_t int_no, uint32_t err_code) {
  kprintf("\n%s - %b\n", exception_messages[int_no], err_code);
  kprintf("cr2: %p\n\n", cpu.cr2);
  isr_debug_dump(cpu, int_no, err_code);

  // handle error

  while (1) // hang
    ;
}

void register_interrupt_handler(uint8_t n, isr_t handler) {
  interrupt_handlers[n] = handler;
}

void irq_handler(registers_t r) {
  /* After every interrupt we need to send an EOI to the PICs
   * or they will not send another interrupt again */
  if (r.int_no >= 40) outb(0xA0, 0x20); /* slave */
  outb(0x20, 0x20);                     /* master */

  /* Handle the interrupt in a more modular way */
  if (interrupt_handlers[r.int_no] != 0) {
    isr_t handler = interrupt_handlers[r.int_no];
    handler(r);
  }
}
