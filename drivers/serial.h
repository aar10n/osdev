//
// Created by Aaron Gill-Braun on 2025-04-24.
//

#ifndef DRIVERS_SERIAL_H
#define DRIVERS_SERIAL_H

#include <kernel/base.h>
#include <kernel/kio.h>

#define COM1 0x3F8
#define COM2 0x2F8
#define COM3 0x3E8
#define COM4 0x2E8

int serial_init(int port);
char serial_read_char(int port);
void serial_write_char(int port, char c);
ssize_t serial_read(int port, size_t off, size_t nmax, kio_t *kio);
ssize_t serial_write(int port, size_t off, size_t nmax, kio_t *kio);

#endif
