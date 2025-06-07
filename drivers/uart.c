//
// Created by Aaron Gill-Braun on 2025-06-02.
//

#include "uart.h"

#include <kernel/device.h>
#include <kernel/tty.h>
#include <kernel/irq.h>
#include <kernel/chan.h>

#include <kernel/printf.h>
#include <kernel/panic.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("uart: " fmt, ##__VA_ARGS__)
#define EPRINTF(fmt, ...) kprintf("uart: %s: " fmt, __func__, ##__VA_ARGS__)

#define IS_VALID_PORT(port) \
  ((port) == COM1 || (port) == COM2 || (port) == COM3 || (port) == COM4)

#define UART_COM13_IRQ 4
#define UART_COM24_IRQ 3

#define UART_DATA 0
#define UART_INTR_EN 1
#define UART_FIFO_CTRL 2
#define UART_LINE_CTRL 3
#define UART_MODEM_CTRL 4
#define UART_LINE_STATUS 5
#define UART_MODEM_STATUS 6

struct uart_dev {
  int number;
  int port;
};

static inline void io_outb(int port, uint8_t value) {
  asm volatile("out dx, al" : : "a"(value), "Nd"(port));
}

static inline uint8_t io_inb(int port) {
  uint8_t value;
  asm volatile("in al, dx" : "=a"(value) : "Nd"(port));
  return value;
}

static mtx_t irq_lock;
static void (*uart_irq_handlers[4])(int, int, void *) = {0};
static void *uart_irq_handler_data[4] = {0};
static chan_t *uart_irq_rx_chan[4] = {0};

// KERNEL_PARAM(


static void uart_irq_port_handler(int port, int index, int irr) {
  int flags;
  int event = 0;
  switch ((irr & 0x6) >> 1) { // bits 1 and 2 indicate interrupt state
    case 0: // modem status change
      DPRINTF("port %d: modem status change\n", port);
      io_inb(port + UART_MODEM_STATUS); // read modem status register to clear
      break;
    case 1: // data received
      DPRINTF("port %d: data received\n", port);
      event = UART_IRQ_RX;
      break;
    case 2: // transmitter holding register empty
      DPRINTF("port %d: transmitter holding register empty\n", port);
      event = UART_IRQ_TX;
      break;
    case 3: // line status change
      DPRINTF("port %d: line status change\n", port);
      uint8_t status = io_inb(port + UART_LINE_STATUS); // read line status register to clear
      event = UART_IRQ_RX;
      if (status & 0x01) {
        flags = UART_EV_OR; // overrun error
      } else if (status & 0x02) {
        flags = UART_EV_PE; // parity error
      } else if (status & 0x04) {
        flags = UART_EV_FE; // framing error
      } else if (status & 0x08) {
        flags = UART_EV_BI; // break interrupt
      } else {
        event = 0;
        flags = 0; // no error
      }
      break;
    default:
      unreachable;
  }

  if (event == 0) {
    return;
  }

  mtx_spin_lock(&irq_lock);
  void (*handler)(int, int, void *) = uart_irq_handlers[index];
  void *data = uart_irq_handler_data[index];
  mtx_spin_unlock(&irq_lock);

  if (handler != NULL) {
    handler(event, flags, data);
  }
}

static void uart_irq_handler(struct trapframe *frame) {
  int port = (int)frame->data; // this narrows down to one set of ports

  int irr;
  if (port == 1) { // irq could have come from COM1 or COM3
    if (((irr = io_inb(COM1 + UART_FIFO_CTRL)) & 0x01)) {
      uart_irq_port_handler(COM1, 0, irr); // interrupt for COM1
    } else if ((irr = io_inb(COM3 + UART_FIFO_CTRL)) & 0x01) {
      uart_irq_port_handler(COM3, 2, irr); // interrupt for COM3
    }
  } else if (port == 2) { // irq could have come from COM2 or COM4
    if ((irr = (io_inb(COM2 + UART_FIFO_CTRL)) & 0x01)) {
      uart_irq_port_handler(COM2, 1, irr); // interrupt for COM2
    } else if ((irr = io_inb(COM4 + UART_FIFO_CTRL)) & 0x01) {
      uart_irq_port_handler(COM4, 3, irr); // interrupt for COM4
    }
  } else {
    unreachable;
  }
}

//

static void uart_irq_static_init() {
  mtx_init(&irq_lock, MTX_SPIN, "uart_irq_lock");
  memset(uart_irq_handlers, 0, ARRAY_SIZE(uart_irq_handlers));

  irq_must_reserve_irqnum(UART_COM13_IRQ);
  irq_must_reserve_irqnum(UART_COM24_IRQ);
  irq_register_handler(UART_COM13_IRQ, uart_irq_handler, (void *)1);
  irq_register_handler(UART_COM24_IRQ, uart_irq_handler, (void *)2);

}
STATIC_INIT(uart_irq_static_init);


bool uart_hw_init_probe(int port) {
  if (!IS_VALID_PORT(port)) {
    EPRINTF("invalid port: %d\n", port);
    return false;
  }

  io_outb(port + UART_INTR_EN, 0x00);     // disable interrupts

  // enable DLAB to set baud rate divisor
  io_outb(port + UART_LINE_CTRL, 0x80);   // DLAB = 1
  io_outb(port + 0, 0x01);                // divisor LSB (115200 baud)
  io_outb(port + 1, 0x00);                // divisor MSB

  io_outb(port + UART_LINE_CTRL, 0x03);   // 8 bits, no parity, one stop bit, DLAB = 0
  io_outb(port + UART_FIFO_CTRL, 0xC7);   // enable FIFO, clear RX/TX, 14-byte threshold
  io_outb(port + UART_MODEM_CTRL, 0x1E);  // loopback mode, set OUT2

  io_outb(port + UART_DATA, 0xAE);        // send test byte
  bool inactive = io_inb(port + UART_DATA) != 0xAE; // read it back
  io_outb(port + UART_MODEM_CTRL, 0x0F);  // set normal mode, clear loopback
  return !inactive;
}

//
// MARK: Hardware API
//

int uart_hw_init(int port) {
  if (!IS_VALID_PORT(port)) {
    EPRINTF("invalid port: %d\n", port);
    return -1;
  }

  if (!uart_hw_init_probe(port)) {
    EPRINTF("port %d probe failed\n", port);
    return -1;
  }
  return 0;
}

int uart_hw_configure(int port, const struct termios *tio) {
  if (!IS_VALID_PORT(port)) {
    EPRINTF("invalid port: %d\n", port);
    return -1;
  }

  // Determine baud rate divisor
  static const int base_baud = 115200;
  speed_t speed = tio->__c_ospeed;
  int divisor;

  switch (speed) {
    case B115200: divisor = 1; break;
    case B57600:  divisor = 2; break;
    case B38400:  divisor = 3; break;
    case B19200:  divisor = 6; break;
    case B9600:   divisor = 12; break;
    case B4800:   divisor = 24; break;
    case B2400:   divisor = 48; break;
    case B1200:   divisor = 96; break;
    default: {
      EPRINTF("unsupported baud rate: %d\n", speed);
      return -1;
    }
  }

  // line control byte
  uint8_t lcr = 0;
  switch (tio->c_cflag & CSIZE) {
    case CS5: lcr |= 0x00; break;
    case CS6: lcr |= 0x01; break;
    case CS7: lcr |= 0x02; break;
    case CS8: lcr |= 0x03; break;
    default: {
      EPRINTF("unsupported data bits: %d\n", tio->c_cflag & CSIZE);
      return -1;
    }
  }

  // line control byte
  if (tio->c_cflag & CSTOPB)
    lcr |= 0x04; // 2 stop bits
  if (tio->c_cflag & PARENB)
    lcr |= 0x08; // parity enable
  if ((tio->c_cflag & PARENB) && !(tio->c_cflag & PARODD))
    lcr |= 0x10; // even parity

  // enable DLAB to set baud divisor
  io_outb(port + UART_LINE_CTRL, lcr | 0x80); // set DLAB
  io_outb(port + 0, divisor & 0xFF);          // LSB
  io_outb(port + 1, (divisor >> 8) & 0xFF);   // MSB

  io_outb(port + UART_LINE_CTRL, lcr);        // clear DLAB, set line control
  io_outb(port + UART_FIFO_CTRL, 0xC7);       // enable FIFO, clear RX/TX, 14-byte threshold

  uint8_t mcr = 0x03; // DTR and RTS
  if (tio->c_cflag & CRTSCTS)
    mcr |= 0x0C;      // enable RTS/CTS (RTS and OUT2)

  io_outb(port + UART_MODEM_CTRL, mcr);       // normal mode, no loopback, set RTS/CTS if requested
  return 0;
}

int uart_hw_set_irq_handler(int port, void (*handler)(int ev, int flags, void *data), void *data) {
  if (!IS_VALID_PORT(port)) {
    EPRINTF("invalid port: %d\n", port);
    return -1;
  }

  mtx_spin_lock(&irq_lock);
  int index;
  int port_irq;
  switch (port) {
    case COM1:
      index = 0; port_irq = UART_COM13_IRQ;
      break;
    case COM2:
      index = 1; port_irq = UART_COM24_IRQ;
      break;
    case COM3:
      index = 2; port_irq = UART_COM13_IRQ;
      break;
    case COM4:
      index = 3; port_irq = UART_COM24_IRQ;
      break;
    default:
      unreachable;
  }

  uart_irq_handlers[index] = handler;
  uart_irq_handler_data[index] = data;
  irq_enable_interrupt(port_irq);

  // enable interrupts for the port
  io_outb(port + UART_INTR_EN, 0x01); // enable data received interrupt
  mtx_spin_unlock(&irq_lock);
  return 0;
}

void uart_hw_unset_irq_handler(int port) {
  if (!IS_VALID_PORT(port)) {
    EPRINTF("invalid port: %d\n", port);
    return;
  }

  mtx_spin_lock(&irq_lock);
  int index;
  int index_compl;
  int port_irq;
  switch (port) {
    case COM1:
      index = 0; index_compl = 2; port_irq = UART_COM13_IRQ;
      break;
    case COM2:
      index = 1; index_compl = 3; port_irq = UART_COM24_IRQ;
      break;
    case COM3:
      index = 2; index_compl = 0; port_irq = UART_COM13_IRQ;
      break;
    case COM4:
      index = 3; index_compl = 1; port_irq = UART_COM24_IRQ;
      break;
    default:
      unreachable;
  }

  uart_irq_handlers[index] = NULL;
  uart_irq_handler_data[index] = NULL;
  if (uart_irq_handlers[index_compl] == NULL) {
    irq_disable_interrupt(port_irq);
  }

  // disable interrupts for the port
  io_outb(port + UART_INTR_EN, 0x00); // disable all interrupts
  mtx_spin_unlock(&irq_lock);
}

int uart_hw_busy_read_ch(int port) {
  if (!IS_VALID_PORT(port)) {
    EPRINTF("invalid port: %d\n", port);
    return -1;
  }

  while (!(io_inb(port + UART_LINE_STATUS) & 0x01)); // wait for rx buffer to be full
  return io_inb(port);
}

void uart_hw_busy_write_ch(int port, char c) {
  if (!IS_VALID_PORT(port)) {
    EPRINTF("invalid port: %d\n", port);
  }
  while (!(io_inb(port + UART_LINE_STATUS) & 0x20)); // wait for tx buffer to be empty
  io_outb(port, c);
}

bool uart_hw_can_read(int port) {
  if (!IS_VALID_PORT(port)) {
    EPRINTF("invalid port: %d\n", port);
    return -1;
  }
  return (io_inb(port + UART_LINE_STATUS) & 0x01) != 0; // check if data is available
}

bool uart_hw_can_write(int port) {
  if (!IS_VALID_PORT(port)) {
    EPRINTF("invalid port: %d\n", port);
    return 0;
  }
  return (io_inb(port + UART_LINE_STATUS) & 0x20) != 0; // check if tx buffer is empty
}

int uart_hw_modem(int port, int command, int arg) {
  if (!IS_VALID_PORT(port)) {
    EPRINTF("invalid port: %d\n", port);
    return -1;
  }

  switch (command) {
    case TTY_MODEM_DTR: // data terminal ready
      if (arg)
        io_outb(port + UART_MODEM_CTRL, io_inb(port + UART_MODEM_CTRL) | 0x01);
      else
        io_outb(port + UART_MODEM_CTRL, io_inb(port + UART_MODEM_CTRL) & ~0x01);
      break;
    case TTY_MODEM_RTS: // request to send
      if (arg)
        io_outb(port + UART_MODEM_CTRL, io_inb(port + UART_MODEM_CTRL) | 0x02);
      else
        io_outb(port + UART_MODEM_CTRL, io_inb(port + UART_MODEM_CTRL) & ~0x02);
      break;
    default:
      EPRINTF("unsupported modem command: %d\n", command);
      return -1;
  }

  return 0;
}

//
// MARK: TTY Device API
//

static void uart_tty_outwakeup(tty_t *tty);

static void uart_tty_input_irq_handler(int ev, int flags, void *data) {
  tty_t *tty = data;
  struct uart_dev *uart_dev = tty->dev_data;

  if (ev == UART_IRQ_RX) {
    // todo: pass input to tty_disc
    todo("uart_irq_rx");
  } else if (ev == UART_IRQ_TX) {
    // todo: write data to uart
    // uart_tty_outwakeup(tty);
  }
}

static int uart_tty_open(tty_t *tty) {
  struct uart_dev *uart_dev = tty->dev_data;
  uart_hw_set_irq_handler(uart_dev->port, uart_tty_input_irq_handler, tty);
  return 0;
}

static void uart_tty_close(tty_t *tty) {
  struct uart_dev *uart_dev = tty->dev_data;
  uart_hw_unset_irq_handler(uart_dev->port);
}

static void uart_tty_outwakeup(tty_t *tty) {
  // // this function is called when the output queue has data to write.
  // struct uart_dev *uart_dev = tty->dev_data;
  // while (tty_outq_peekch(tty->outq) >= 0) {
  //   if (!uart_hw_can_write(uart_dev->port))
  //     break;
  //
  //   int ch = tty_outq_getch(tty->outq);
  //   if (ch < 0) {
  //     EPRINTF("error reading from output queue\n");
  //     break;
  //   }
  //
  //   uart_hw_busy_write_ch(uart_dev->port, (char)ch);
  // }
}

static int uart_tty_ioctl(tty_t *tty, unsigned long request, void *arg) {
  // no custom ioctls for now
  return 0;
}

static int uart_tty_update(tty_t *tty, struct termios *termios) {
  struct uart_dev *uart_dev = tty->dev_data;
  return uart_hw_configure(uart_dev->port, termios);
}

static int uart_tty_modem(tty_t *tty, int command, int arg) {
  struct uart_dev *uart_dev = tty->dev_data;
  return uart_hw_modem(uart_dev->port, command, arg);
}

static bool uart_tty_isbusy(tty_t *tty) {
  struct uart_dev *uart_dev = tty->dev_data;
  return !uart_hw_can_write(uart_dev->port);
}

static struct ttydev_ops uart_ttydev_ops = {
  .tty_open = uart_tty_open,
  .tty_close = uart_tty_close,
  .tty_outwakeup = uart_tty_outwakeup,
  .tty_ioctl = uart_tty_ioctl,
  .tty_update = uart_tty_update,
  .tty_modem = uart_tty_modem,
  .tty_isbusy = uart_tty_isbusy,
};

//
// MARK: Device API
//

static int uart_dev_open(device_t *dev, int flags) {
  return 0;
}

static int uart_dev_close(device_t *dev) {
  return 0;
}

static ssize_t uart_dev_read(device_t *dev, size_t off, size_t nmax, kio_t *kio) {
  struct uart_dev *uart_dev = dev->data;
  ssize_t n = 0;
  while (nmax--) {
    int ch = uart_hw_busy_read_ch(uart_dev->port);
    if (ch < 0)
      return -1; // error reading from serial port
    if (kio_write_ch(kio, (char)ch) < 0)
      return -1; // error writing to kio
    n++;
  }
  return n;
}

static ssize_t uart_dev_write(device_t *dev, size_t off, size_t nmax, kio_t *kio) {
  struct uart_dev *uart_dev = dev->data;
  ssize_t n = 0;
  char ch;
  while (nmax-- && kio_read_ch(&ch, kio) > 0) {
    uart_hw_busy_write_ch(uart_dev->port, ch);
    n++;
  }
  return n;
}

static struct device_ops uart_ops = {
  .d_open = uart_dev_open,
  .d_close = uart_dev_close,
  .d_read = uart_dev_read,
  .d_write = uart_dev_write,
};

static void register_serial_devices() {
  static const int ports[] = { COM1, COM2, COM3, COM4 };
  for (int i = 0; i < ARRAY_SIZE(ports); i++) {
    if (!uart_hw_init_probe(ports[i])) {
      continue;
    }

    struct uart_dev *uart_dev = kmallocz(sizeof(struct uart_dev));
    uart_dev->number = i + 1; // COM1 is 1, COM2 is 2, etc.
    uart_dev->port = ports[i];

    device_t *dev = alloc_device(uart_dev, &uart_ops);
    if (register_dev("serial", dev) < 0) {
      DPRINTF("failed to register device");
      dev->data = NULL;
      free_device(dev);
      kfree(uart_dev);
    }
  }
}
MODULE_INIT(register_serial_devices);
