//
// Created by Aaron Gill-Braun on 2020-09-20.
//

#include <kernel/cpu/asm.h>
#include <kernel/cpu/pic.h>

uint8_t slave_offset = 0;

void pic_remap(uint8_t offset1, uint8_t offset2) {
  slave_offset = offset2;
  
  uint8_t pic1_mask = inb(PIC1_DATA);
  uint8_t pic2_mask = inb(PIC2_DATA);

  // initialize both pics
  outb(PIC1_COMMAND, ICW1_INIT);
  outb(PIC2_COMMAND, ICW1_INIT);

  // set irq base offsets
  outb(PIC1_DATA, offset1);
  outb(PIC2_DATA, offset2);

  // set up master <-> slave communication
  outb(PIC1_DATA, 0x04);
  outb(PIC2_DATA, 0x02);

  // set both pics to run in 8086/88 mode
  outb(PIC1_DATA, ICW4_8086);
  outb(PIC2_DATA, ICW4_8086);

  // write back the saved masks
  outb(PIC1_DATA, 0);
  outb(PIC2_DATA, 0);
}

void pic_send_eoi(uint32_t vector) {
  // Send EOI to PIC
  if (vector >= slave_offset) outb(PIC2_COMMAND, PIC_EOI); /* slave */
  outb(PIC1_COMMAND, PIC_EOI);                             /* master */
}

void pic_disable() {
  // // initialize both pics
  // outb(PIC1_COMMAND, ICW1_INIT);
  // outb(PIC2_COMMAND, ICW1_INIT);
  //
  // // set irq base offsets
  // outb(PIC1_DATA, 0x20);
  // outb(PIC2_DATA, 0x28);
  //
  // // set up master <-> slave communication
  // outb(PIC1_DATA, 0x04);
  // outb(PIC2_DATA, 0x02);
  //
  // // set both pics to run in 8086/88 mode
  // outb(PIC1_DATA, ICW4_8086);
  // outb(PIC2_DATA, ICW4_8086);
  //
  // // write back the saved masks
  // outb(PIC1_DATA, 0xFF);
  // outb(PIC2_DATA, 0xFF);

  // initialize both pics
  outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
  outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);

  // set irq base offsets
  outb(PIC1_DATA, 0xE0);
  outb(PIC2_DATA, 0xE8);

  // set up master <-> slave communication
  outb(PIC1_DATA, 0x04);
  outb(PIC2_DATA, 0x02);

  outb(PIC1_DATA, 1);
  outb(PIC2_DATA, 1);

  // mask all interrupts
  outb(PIC1_DATA, 0xFF);
  outb(PIC2_DATA, 0xFF);
}
