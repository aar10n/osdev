//
// Created by Aaron Gill-Braun on 2025-06-02.
//

#ifndef DRIVERS_TTY_UART_H
#define DRIVERS_TTY_UART_H

#include <kernel/base.h>
#include <abi/termios.h>

#define COM1 0x3F8
#define COM2 0x2F8
#define COM3 0x3E8
#define COM4 0x2E8

// uart irq events
#define UART_IRQ_RX  0x01
#define UART_IRQ_TX  0x02
#define UART_IRQ_DCD 0x03

// uart event flags
#define UART_EV_OR 0x01  // overrun error
#define UART_EV_PE 0x02  // parity error
#define UART_EV_FE 0x04  // framing error
#define UART_EV_BI 0x08  // break interrupt


int uart_hw_init(int port);
int uart_hw_configure(int port, const struct termios *termios);
int uart_hw_set_irq_handler(int port, void (*handler)(int ev, int ev_data, void *data), void *data);
void uart_hw_unset_irq_handler(int port);

int uart_hw_busy_read_ch(int port);
void uart_hw_busy_write_ch(int port, char c);
bool uart_hw_can_read(int port);
bool uart_hw_can_write(int port);
int uart_hw_modem(int port, int command, int arg);

#endif
