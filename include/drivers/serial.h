//
// Created by Aaron Gill-Braun on 2019-04-20.
//

#ifndef DRIVERS_SERIAL_H
#define DRIVERS_SERIAL_H

#define COM1 0x3F8
#define COM2 0x2F8
#define COM3 0x3E8
#define COM4 0x2E8

void init_serial(int port);
void serial_write(int port, char *s);
void serial_write_char(int port, char a);

#endif //DRIVERS_SERIAL_H
