//
// Created by Aaron Gill-Braun on 2020-09-30.
//

#ifndef DRIVERS_SERIAL_H
#define DRIVERS_SERIAL_H

#define COM1 0x3F8
#define COM2 0x2F8
#define COM3 0x3E8
#define COM4 0x2E8

void serial_init(int port);
void serial_write(int port, const char *s);
void serial_nwrite(int port, const char *s, size_t l);

#endif
