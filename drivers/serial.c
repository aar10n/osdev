//
// Created by Aaron Gill-Braun on 2020-09-30.
//

#include <string.h>
#include <lock.h>
#include <cpu/io.h>
#include <drivers/serial.h>

static spinlock_t serial_lock;

void serial_init(int port) {
  outb(port + 1, 0x00); // Disable all interrupts
  outb(port + 3, 0x80); // Enable DLAB (set baud rate divisor)
  outb(port + 0, 0x03); // Set divisor to 3 (lo byte) 38400 baud
  outb(port + 1, 0x00); //                  (hi byte)
  outb(port + 3, 0x03); // 8 bits, no parity, one stop bit
  outb(port + 2, 0xC7); // Enable FIFO, clear them, with 14-byte threshold
  outb(port + 4, 0x0B); // IRQs enabled, RTS/DSR set
}

void serial_write_char(int port, char a) {
  while(!(inb(port + 5) & 0x20)); // wait for empty
  outb(port, a);
}

void serial_write(int port, const char *s) {
  lock(serial_lock);
  for (int i = 0; i < strlen(s); i++) {
    serial_write_char(port, s[i]);
  }
  unlock(serial_lock);
}

void puts(const char *str) {
  serial_write(COM1, str);
}
