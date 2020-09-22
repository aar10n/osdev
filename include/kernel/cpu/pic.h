//
// Created by Aaron Gill-Braun on 2020-09-20.
//

#ifndef KERNEL_CPU_PIC_H
#define KERNEL_CPU_PIC_H

#include <stdint.h>

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

#define PIC_INIT 0x11
#define PIC_EOI  0x20
#define PIC_8086 0x01

void pic_remap(uint8_t offset1, uint8_t offset2);
void pic_send_eoi(uint32_t vector);
void pic_disable();

#endif
