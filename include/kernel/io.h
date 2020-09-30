//
// Created by Aaron Gill-Braun on 2020-09-26.
//

#ifndef KERNEL_IO_H
#define KERNEL_IO_H

#include <stdint.h>

void outb(uint16_t port, uint8_t data);
uint8_t inb(uint16_t port);

void outw(uint16_t port, uint16_t data);
uint16_t inw(uint16_t port);

void outdw(uint16_t port, uint32_t data);
uint32_t indw(uint16_t port);

#endif
