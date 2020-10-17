//
// Created by Aaron Gill-Braun on 2020-09-20.
//

#include <cpu/idt.h>
#include <cpu/io.h>
#include <device/pic.h>
#include <vectors.h>

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

extern void ignore_irq();

void pic_init() {
  // initialize both pics
  outb(PIC1_COMMAND, 0x11);
  outb(PIC2_COMMAND, 0x11);

  // set irq base offsets
  outb(PIC1_DATA, VECTOR_PIC_IRQ0);
  outb(PIC2_DATA, VECTOR_PIC_IRQ8);

  // set the pics to run in cascade mode
  outb(PIC1_DATA, 0x04);
  outb(PIC2_DATA, 0x02);

  // 8086/88 Mode + Auto EOI
  outb(PIC1_DATA, 0x3);
  outb(PIC2_DATA, 0x3);

  // // mask all interrupts
  outb(PIC1_DATA, 0xFF);
  outb(PIC2_DATA, 0xFF);

  // set the relevant gates to use the pic handler
  idt_gate_t gate = gate((uintptr_t) ignore_irq, KERNEL_CS, 0, INTERRUPT_GATE, 0, 1);
  for (int i = VECTOR_PIC_IRQ0; i <= VECTOR_PIC_IRQ7; i++) {
    idt_set_gate(i, gate);
  }
  for (int i = VECTOR_PIC_IRQ8; i <= VECTOR_PIC_IRQ15; i++) {
    idt_set_gate(i, gate);
  }
}
