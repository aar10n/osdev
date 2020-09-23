//
// Created by Aaron Gill-Braun on 2019-04-19.
//

#ifndef DRIVERS_KEYBOARD_H
#define DRIVERS_KEYBOARD_H

#define KBD_REG_DATA    0x60
#define KBD_REG_STATUS  0x64
#define KBD_REG_COMMAND 0x64

typedef union {
  uint8_t raw;
  struct {
    uint8_t output_ready : 1;
    uint8_t input_busy : 1;
    uint8_t reset_ok : 1;
    uint8_t last : 1;
    uint8_t __unused : 1;
    uint8_t tx_timeout : 1;
    uint8_t rx_timeout : 1;
    uint8_t parity_error : 1;
  };
} i8042_status_t;

void init_keyboard();

#endif // DRIVERS_KEYBOARD_H
