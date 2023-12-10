//
// Created by Aaron Gill-Braun on 2023-12-10.
//

#include <kernel/device/8250.h>

#include <kernel/cpu/io.h>

#define SERIAL_DATA 0
#define SERIAL_INTR_EN 1
#define SERIAL_FIFO_CTRL 2
#define SERIAL_LINE_CTRL 3
#define SERIAL_MODEM_CTRL 4
#define SERIAL_LINE_STATUS 5
#define SERIAL_MODEM_STATUS 6

int serial_port_init(uint16_t port) {
  outb(port + SERIAL_INTR_EN, 0x00);    // disable interrupts
  outb(port + SERIAL_LINE_CTRL, 0x80);  // set baud rate divisor
  outw(port + SERIAL_DATA, 0x01);       // 115200 baud
  outb(port + SERIAL_LINE_CTRL, 0x03);  // 8 bits, one stop bit, no parity
  outb(port + SERIAL_FIFO_CTRL, 0xC7);  // enable FIFO, clear, 14-byte threshold
  outb(port + SERIAL_MODEM_CTRL, 0x0B); // enable IRQs, RTS/DSR set
  outb(port + SERIAL_MODEM_CTRL, 0x1E); // set in loopback mode, test the serial chip
  outb(port + SERIAL_DATA, 0xAE);       // send the test character

  // check that the port sends back the test character
  if (inb(port + SERIAL_DATA) != 0xAE) {
    return -1;
  }

  outb(port + SERIAL_MODEM_CTRL, 0x0F); // reset the serial chip
  return 0;
}

int serial_port_read_char(uint16_t port, char *ch) {
  while (!(inb(port + SERIAL_LINE_STATUS) & 0x01)); // wait for rx buffer to be full
  *ch = (char) inb(port);
  return 0;
}

int serial_port_write_char(uint16_t port, char ch) {
  while (!(inb(port + SERIAL_LINE_STATUS) & 0x20)); // wait for tx buffer to be empty
  outb(port, ch);
  return 0;
}
