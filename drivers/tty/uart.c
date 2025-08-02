//
// Created by Aaron Gill-Braun on 2025-06-02.
//

#include "uart.h"

#include <kernel/device.h>
#include <kernel/console.h>
#include <kernel/chan.h>
#include <kernel/tty.h>
#include <kernel/irq.h>
#include <kernel/proc.h>
#include <kernel/params.h>

#include <kernel/printf.h>
#include <kernel/panic.h>

#include <fs/devfs/devfs.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("uart: " fmt, ##__VA_ARGS__)
//#define DPRINTF(fmt, ...)
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

KERNEL_PARAM("console.uart.port", str_t, console_uart_port_param, str_null);
static int console_uart_port;

struct uart_dev {
  int number;
  int port;
  pid_t tx_tid;
};

struct uart_irq {
  int port;
  int index;
  int event;
  int data;
};

static inline void io_outb(int port, uint8_t value) {
  asm volatile("out dx, al" : : "a"(value), "Nd"(port));
}

static inline volatile uint8_t io_inb(int port) {
  uint8_t value;
  asm volatile("in al, dx" : "=a"(value) : "Nd"(port));
  return value;
}

static void uart_irq_handler(struct trapframe *frame);
static int uart_softirq_handler();

static mtx_t irq_lock;
static void (*uart_irq_handlers[4])(int, int, void *) = {0};
static void *uart_irq_handler_data[4] = {0};
static chan_t *uart_softirq_chan = {0};
pid_t uart_softirq_pid = -1;

static void uart_static_init() {
  if (str_eq_charp(console_uart_port_param, "com1")) {
    console_uart_port = COM1;
  } else if (str_eq_charp(console_uart_port_param, "com2")) {
    console_uart_port = COM2;
  } else if (str_eq_charp(console_uart_port_param, "com3")) {
    console_uart_port = COM3;
  } else if (str_eq_charp(console_uart_port_param, "com4")) {
    console_uart_port = COM4;
  } else {
    EPRINTF("invalid console uart port: {:str}\n", &console_uart_port_param);
    console_uart_port = COM4;
  }

  // initialize static variables
  mtx_init(&irq_lock, MTX_SPIN, "uart_irq_lock");
  memset(uart_irq_handlers, 0, ARRAY_SIZE(uart_irq_handlers));

  irq_must_reserve_irqnum(UART_COM13_IRQ);
  irq_must_reserve_irqnum(UART_COM24_IRQ);
  irq_register_handler(UART_COM13_IRQ, uart_irq_handler, (void *)1);
  irq_register_handler(UART_COM24_IRQ, uart_irq_handler, (void *)2);

  uart_softirq_chan = chan_alloc(128, sizeof(struct uart_irq), CHAN_NOBLOCK, "uart_softirq");
}
STATIC_INIT(uart_static_init);

static void start_softirq_handler() {
  __ref proc_t *softirq_proc = proc_alloc_new(getref(curproc->creds));
  uart_softirq_pid = softirq_proc->pid;
  proc_setup_add_thread(softirq_proc, thread_alloc(TDF_KTHREAD, SIZE_16KB));
  proc_setup_entry(softirq_proc, (uintptr_t) uart_softirq_handler, 0);
  proc_setup_name(softirq_proc, cstr_make("uart_softirq"));
  proc_finish_setup_and_submit_all(moveref(softirq_proc));
}
MODULE_INIT(start_softirq_handler);


static void uart_irq_port_handler(int port, int index, int irr) {
  // DPRINTF("irq: port %d: irr = 0x%x\n", port, irr);
  uint8_t status;
  int event = 0;
  int data = 0;
  switch ((irr & 0x6) >> 1) { // bits 1 and 2 indicate interrupt state
    case 0: // modem status change
      DPRINTF("port %d: modem status change\n", port);
      status = io_inb(port + UART_MODEM_STATUS); // read modem status register to clear
      uint8_t dcd_delta = (status >> 3) & 1; // bit 3 indicates DCD change since last read
      if (dcd_delta) {
        event = UART_IRQ_DCD;
        data = (status & 0x80) ? 1 : 0; // bit 7 indicates DCD state
      }
      break;
    case 1: // transmitter holding register empty
      DPRINTF("port %d: transmitter holding register empty\n", port);
      event = UART_IRQ_TX;
      break;
    case 2: // data received
      DPRINTF("port %d: data received\n", port);
      event = UART_IRQ_RX;
      break;
    case 3: // line status change
      DPRINTF("port %d: line status change\n", port);
      status = io_inb(port + UART_LINE_STATUS); // read line status register to clear
      event = UART_IRQ_RX;
      if (status & 0x01) {
        data = UART_EV_OR; // overrun error
      } else if (status & 0x02) {
        data = UART_EV_PE; // parity error
      } else if (status & 0x04) {
        data = UART_EV_FE; // framing error
      } else if (status & 0x08) {
        data = UART_EV_BI; // break interrupt
      } else {
        event = 0;
        data = 0; // no error
      }
      break;
    default:
      unreachable;
  }

  if (event == 0) {
    return;
  }

  // pass the irq on to the softirq handler process
  struct uart_irq irq = {
    .port = port,
    .index = index,
    .event = event,
    .data = data,
  };
  if (chan_send(uart_softirq_chan, &irq) < 0) {
    EPRINTF("failed to send uart irq to softirq handler\n");
    return;
  }
}

static void uart_irq_handler(struct trapframe *frame) {
  int port = (int)frame->data; // this narrows down to one set of ports
  // DPRINTF("uart irq handler: port %d\n", port);

  int irr;
  if (port == 1) { // irq could have come from COM1 or COM3
    if (!((irr = io_inb(COM1 + UART_FIFO_CTRL)) & 0x01)) {
      uart_irq_port_handler(COM1, 0, irr); // interrupt for COM1
    } else if (!((irr = io_inb(COM3 + UART_FIFO_CTRL)) & 0x01)) {
      uart_irq_port_handler(COM3, 2, irr); // interrupt for COM3
    }
  } else if (port == 2) { // irq could have come from COM2 or COM4
    if (!(irr = (io_inb(COM2 + UART_FIFO_CTRL)) & 0x01)) {
      uart_irq_port_handler(COM2, 1, irr); // interrupt for COM2
    } else if (!((irr = io_inb(COM4 + UART_FIFO_CTRL)) & 0x01)) {
      uart_irq_port_handler(COM4, 3, irr); // interrupt for COM4
    }
  } else {
    unreachable;
  }
}

static int uart_softirq_handler() {
  DPRINTF("starting uart softirq handler\n");

  struct uart_irq irq;
  while (chan_recv(uart_softirq_chan, &irq) == 0) {
    DPRINTF("softirq handler received irq: port %d, index %d, event %d, data 0x%x\n",
            irq.port, irq.index, irq.event, irq.data);
    mtx_spin_lock(&irq_lock);
    void (*handler)(int, int, void *) = uart_irq_handlers[irq.index];
    void *data = uart_irq_handler_data[irq.index];
    mtx_spin_unlock(&irq_lock);

    if (handler != NULL) {
      handler(irq.event, irq.data, data);
    }
  }

  DPRINTF("softirq channel closed, exiting handler\n");
  return 0;
}

//

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
  unsigned char lcr = 0;

  // determine word length
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

  // stop bits
  if (tio->c_cflag & CSTOPB)
    lcr |= 0x04;

  // parity
  if (tio->c_cflag & PARENB) {
    lcr |= 0x08;
    if (!(tio->c_cflag & PARODD))
      lcr |= 0x10;
  }

  // baud rate
  unsigned int baud;
  switch (tio->__c_ospeed) {
    case B1200: baud = 1200; break;
    case B1800: baud = 1800; break;
    case B2400: baud = 2400; break;
    case B4800: baud = 4800; break;
    case B9600: baud = 9600; break;
    case B19200: baud = 19200; break;
    case B38400: baud = 38400; break;
    case B57600: baud = 57600; break;
    case B115200: baud = 115200; break;
    default: {
      EPRINTF("unsupported baud rate %d\n", tio->__c_ospeed);
      return -1;
    }
  }

  unsigned short divisor = 115200 / baud;

  // enable DLAB
  io_outb(port + UART_LINE_CTRL, lcr | 0x80);
  io_outb(port + UART_DATA, divisor & 0xFF);
  io_outb(port + UART_INTR_EN, (divisor >> 8) & 0xFF);
  io_outb(port + UART_LINE_CTRL, lcr);

  // // enable FIFO, clear TX/RX queues, 14-byte threshold
  // io_outb(port + UART_FIFO_CTRL, 0xC7);
  io_outb(port + UART_FIFO_CTRL, 0x00); // disable FIFO

  // modem control: DTR, RTS, OUT2
  io_outb(port + UART_MODEM_CTRL, 0xf);
  return 0;
}

int uart_hw_set_irq_handler(int port, void (*handler)(int ev, int ev_data, void *data), void *data) {
  if (!IS_VALID_PORT(port)) {
    EPRINTF("invalid port: %d\n", port);
    return -1;
  }

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

  mtx_spin_lock(&irq_lock);
  uart_irq_handlers[index] = handler;
  uart_irq_handler_data[index] = data;
  irq_enable_interrupt(port_irq);

  // enable interrupts for the port
  io_outb(port + UART_INTR_EN, 0x9); // enable data received/modem status irqs
  // io_outb(port + UART_INTR_EN, 0b1101);
  mtx_spin_unlock(&irq_lock);
  return 0;
}

void uart_hw_unset_irq_handler(int port) {
  if (!IS_VALID_PORT(port)) {
    EPRINTF("invalid port: %d\n", port);
    return;
  }

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

  mtx_spin_lock(&irq_lock);
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

  if (command == 0 && arg == 0) {
    // query current modem status
    uint8_t status = io_inb(port + UART_MODEM_STATUS);
    int bits = 0;
    bits |= (status & 0x10) ? TTY_MODEM_BM_CTS : 0; // cts
    bits |= (status & 0x20) ? TTY_MODEM_BM_DSR : 0; // dsr
    bits |= (status & 0x80) ? TTY_MODEM_BM_DCD : 0; // dcd
    bits |= (status & 0x40) ? TTY_MODEM_BM_RI : 0;  // ri
    return bits;
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

static int uart_tty_transmit_thread(tty_t *tty) {
  struct uart_dev *uart_dev = tty->dev_data;
  DPRINTF("transmit thread started for tty on port %d\n", uart_dev->port);

  if (!tty_lock(tty)) {
    EPRINTF("tty device is gone\n");
    return -ENXIO;
  }

  int res = 0;
  while (true) {
    if (ttyoutq_peek_ch(tty->outq) < 0) {
      // no data available, wait for it
      if ((res = tty_wait_cond(tty, &tty->out_wait)) < 0)
        break; // device is done
      continue;
    }

    // write data to the uart
    uart_tty_outwakeup(tty);
  }

  DPRINTF("transmit thread exiting\n");
  tty_unlock(tty);
  return res;
}

// this is run in a softirq context, so it may block
static void uart_tty_input_irq_handler(int ev, int ev_data, void *data) {
  tty_t *tty = data;
  struct uart_dev *uart_dev = tty->dev_data;

  if (!tty_lock(tty)) {
    EPRINTF("tty device is gone\n");
    return;
  }

  if (ev == UART_IRQ_RX) {
    int discflags = 0;
    discflags |= (ev_data & UART_EV_PE) ? TTY_IN_PARITY : 0;
    discflags |= (ev_data & UART_EV_FE) ? TTY_IN_FRAMING : 0;
    discflags |= (ev_data & UART_EV_BI) ? TTY_IN_BREAK : 0;

    // read data from uart and pass to ttydisc
    while (uart_hw_can_read(uart_dev->port)) {
      int ch = uart_hw_busy_read_ch(uart_dev->port);
      DPRINTF("received character '%#c' (%#x) on port %d\n", (char) ch, ch, uart_dev->port);
      if (ch < 0) {
        EPRINTF("error reading from uart port %d\n", uart_dev->port);
        goto done;
      }

      // pass the character to the tty input handler
      if (ttydisc_rint(tty, (uint8_t) ch, discflags) < 0) {
        EPRINTF("error handling input character %#c\n", ch);
        goto done;
      }
    }
    ttydisc_rint_done(tty);
  } else if (ev == UART_IRQ_TX) {
    // not used right now
    DPRINTF("output ready\n");
    // TODO: signal that tty can continue writing output
  } else if (ev == UART_IRQ_DCD) {
    DPRINTF("data carrier detect changed (dcd=%d)\n", ev_data);
    if (data) {
      // data carrier connected
      tty->flags |= TTYF_DCDRDY;
    } else {
      // data carrier disconnected
      tty->flags &= ~TTYF_DCDRDY;
    }
    tty_signal_cond(tty, &tty->dcd_wait);
  }

LABEL(done);
  tty_unlock(tty);
}

static int uart_tty_open(tty_t *tty) {
  struct uart_dev *uart_dev = tty->dev_data;
  DPRINTF("opening tty on port %d\n", uart_dev->port);
  ASSERT(uart_dev->tx_tid == -1);
  uart_hw_set_irq_handler(uart_dev->port, uart_tty_input_irq_handler, tty);
  uart_hw_modem(uart_dev->port, TTY_MODEM_DTR, 1);

  int modem = uart_hw_modem(uart_dev->port, 0, 0);
  DPRINTF("modem status for port %d: 0x%x\n", uart_dev->port, modem);
  if (modem & TTY_MODEM_BM_DCD) {
    // data carrier detect is ready
    tty->flags |= TTYF_DCDRDY;
  }

  // start a new thread under the uart process to handle transmission
  thread_t *thread = thread_alloc(TDF_KTHREAD, SIZE_16KB);
  thread_setup_name(thread, cstr_make("uart_tty_transmit"));
  thread_setup_entry(thread, (uintptr_t) uart_tty_transmit_thread, 1, tty);

  __ref proc_t *uart_proc = proc_lookup(uart_softirq_pid);
  ASSERT(uart_proc != NULL);
  proc_add_thread(uart_proc, thread);
  uart_dev->tx_tid = thread->tid;
  pr_putref(&uart_proc);
  return 0;
}

static void uart_tty_close(tty_t *tty) {
  struct uart_dev *uart_dev = tty->dev_data;
  ASSERT(uart_dev->tx_tid != -1);
  uart_hw_unset_irq_handler(uart_dev->port);
  uart_hw_modem(uart_dev->port, TTY_MODEM_DTR, 0);

  __ref proc_t *uart_proc = proc_lookup(uart_softirq_pid);
  ASSERT(uart_proc != NULL);
  proc_kill_tid(uart_proc, uart_dev->tx_tid, 0, SIGTERM);
  pr_putref(&uart_proc);

  uart_dev->tx_tid = -1;
}

static void uart_tty_outwakeup(tty_t *tty) {
  // this function is called when the output queue has data to write.
  struct uart_dev *uart_dev = tty->dev_data;
  while (ttyoutq_peek_ch(tty->outq) >= 0) {
    if (!uart_hw_can_write(uart_dev->port))
      break;

    int ch = ttyoutq_get_ch(tty->outq);
    if (ch < 0) {
      EPRINTF("error reading from output queue\n");
      break;
    }

    uart_hw_busy_write_ch(uart_dev->port, (char)ch);
  }
}

static int uart_tty_ioctl(tty_t *tty, unsigned long request, void *arg) {
  // no custom ioctls for now
  return -ENOTSUP;
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

static ssize_t uart_dev_read(device_t *dev, _unused size_t off, size_t nmax, kio_t *kio) {
  struct uart_dev *uart_dev = dev->data;
  ssize_t n = 0;
  while (nmax--) {
    int ch = uart_hw_busy_read_ch(uart_dev->port);
    if (ch < 0)
      return ch; // error reading from serial port
    if (kio_write_ch(kio, (char)ch) < 0)
      break; // kio buffer is full
    n++;
  }
  return n;
}

static ssize_t uart_dev_write(device_t *dev, _unused size_t off, size_t nmax, kio_t *kio) {
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

  devfs_register_class(dev_major_by_name("serial"), -1, "ttyS", DEVFS_NUMBERED);
  for (int i = 0; i < ARRAY_SIZE(ports); i++) {
    if (!uart_hw_init_probe(ports[i])) {
      continue;
    }

    struct uart_dev *uart_dev = kmallocz(sizeof(struct uart_dev));
    uart_dev->number = i + 1; // COM1 is 1, COM2 is 2, etc.
    uart_dev->port = ports[i];
    uart_dev->tx_tid = -1;

    tty_t *tty = tty_alloc(&uart_ttydev_ops, uart_dev);
    if (tty == NULL) {
      EPRINTF("failed to allocate tty for serial port %d\n", i+1);
      kfree(uart_dev);
      continue;
    }

    device_t *dev = alloc_device(tty, NULL, NULL);
    if (register_dev("serial", dev) < 0) {
      EPRINTF("failed to register device");
      dev->data = NULL;
      free_device(dev);
      tty_free(&tty);
      kfree(uart_dev);
      continue;
    }

    if (ports[i] == console_uart_port) {
      console_t *console = kmallocz(sizeof(console_t));
      console->name = "uart";
      console->tty = tty;
      console_register(console);
    }
  }
}
MODULE_INIT(register_serial_devices);
