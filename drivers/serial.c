//
// Created by Aaron Gill-Braun on 2020-09-30.
//

#include <drivers/serial.h>
#include <cpu/io.h>
#include <string.h>
#include <printf.h>

typedef enum serial_reg {
  SERIAL_DATA       = 0x00, // data register
  SERIAL_INTR_EN    = 0x01, // interrupt enable register
  SERIAL_FIFO_CTRL  = 0x02, // FIFO control register
  SERIAL_LINE_CTRL  = 0x03, // line control register
  SERIAL_MODEM_CTRL = 0x04, // modem control register
  SERIAL_LINE_STS   = 0x05, // line status register
  SERIAL_MODEM_STS  = 0x06, // model status register
  SERIAL_SCRATCH    = 0x07, // scratch register
} serial_reg_t;


#define DATA_READY_INT  0b00000001
#define THR_EMPTY_INT   0b00000010
#define STATUS_INT      0b00000100
#define MODEM_STS_INT   0b00001000

#define FIFO_ENABLE   0b00000001
//
#define CLEAR_RX      0b00000010
#define CLEAR_TX      0b00000100
//
#define TRIGGER_1     0b00000000
#define TRIGGER_4     0b01000000
#define TRIGGER_8     0b10000000
#define TRIGGER_14    0b11000000

#define LENGTH_5_BITS 0b00000000
#define LENGTH_6_BITS 0b00000001
#define LENGTH_7_BITS 0b00000010
#define LENGTH_8_BITS 0b00000011
//
#define STOP_BITS     0b00000100
//
#define NO_PARITY     0b00000000
#define ODD_PARITY    0b00001000
#define EVEN_PARITY   0b00011000
#define MARK_PARITY   0b00101000
#define SPACE_PARITY  0b00111000
//
#define FORCE_BREAK   0b01000000
//
#define DLAB          0b10000000

#define DTR           0b00000001
#define RTS           0b00000010
#define OUT1          0b00000100
#define OUT2          0b00001000
#define LOOPBACK      0b00010000

void serial_init(int port) {
  outb(port + SERIAL_INTR_EN, 0); // disable interrupts
  outb(port + SERIAL_FIFO_CTRL, FIFO_ENABLE | TRIGGER_14);
  outb(port + SERIAL_LINE_CTRL, DLAB);
  outw(port + SERIAL_DATA, 1); // 115200 baud
  outb(port + SERIAL_LINE_CTRL, LENGTH_8_BITS | NO_PARITY);
  outb(port + SERIAL_MODEM_CTRL, OUT1);
}

void serial_write_char(int port, char a) {
  while(!(inb(port + 5) & 0x20)); // wait for empty
  outb(port, a);
}

void serial_write(int port, const char *s) {
  size_t l = strlen(s);
  serial_nwrite(port, s, l);
}

void serial_nwrite(int port, const char *s, size_t l) {
  for (int i = 0; i < l; i++) {
    serial_write_char(port, s[i]);
  }
}
