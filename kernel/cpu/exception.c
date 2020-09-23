//
// Created by Aaron Gill-Braun on 2020-08-25.
//

#include <kernel/cpu/exception.h>
#include <kernel/mm/mm.h>
#include <stdio.h>

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
  kprintf("  cr0: %#b\n", cpu.cr0);
  kprintf("  cr2: %p\n", cpu.cr2);
  kprintf("  cr3: %p\n", cpu.cr3);
  kprintf("  cr4: %#b\n", cpu.cr4);
}

// Handle severe and non-ignorable errors
_Noreturn void exception_handler(cpu_t cpu, uint32_t int_no, uint32_t err_code) {
  kprintf("\n%s - %#08b\n", exception_messages[int_no], err_code);
  kprintf("cr2: %p\n\n", cpu.cr2);
  kprintf("cr2: %p\n", phys_to_virt(cpu.cr2));
  isr_debug_dump(cpu, int_no, err_code);

  // handle error

  // hang
  while (true) {
    __asm("hlt");
  }
}
