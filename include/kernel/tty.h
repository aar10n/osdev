//
// Created by Aaron Gill-Braun on 2023-12-20.
//

#ifndef KERNEL_TTY_H
#define KERNEL_TTY_H

#include <kernel/base.h>
#include <kernel/mutex.h>
#include <kernel/cond.h>
#include <kernel/kevent.h>

#include <kernel/tty/ttydisc.h>
#include <kernel/tty/ttyqueue.h>

#include <abi/termios.h>

struct pgroup;
struct session;

struct ttydev_ops;


typedef struct tty {
  uint32_t flags;             // tty flags
  mtx_t lock;                 // tty lock
  uint32_t owners;            // number of owners (open count)

  struct ttyinq *inq;         // input queue
  struct ttyoutq *outq;       // output queue
  struct knlist knlist;       // associated knotes

  cond_t in_wait;	            // input wait cond
  cond_t out_wait;	          // output wait cond
  cond_t dcd_wait;            // DCD (data carrier detect) wait cond

  struct termios termios;     // terminal attributes
  struct winsize winsize;     // window size
  uint32_t column;            // current column position

  struct pgroup *pgrp;        // foreground process group
  struct session *session;    // associated session

  struct ttydev_ops *dev_ops; // device operations
  void *dev_data;             // device-specific data
} tty_t;

// tty flags
#define TTYF_OPENED   0x0001    // tty is opened
#define TTYF_GONE     0x0002    // tty device is gone
#define TTYF_STOPPED  0x0004    // tty is stopped (output suspended)
#define TTYF_DCDRDY   0x0008    // tty data carrier detect is ready
#define TTYF_NONBLOCK 0x0010    // tty is in non-blocking mode


struct ttydev_ops {
  int (*tty_open)(tty_t *tty);
  void (*tty_close)(tty_t *tty);
  void (*tty_outwakeup)(tty_t *tty);

  int (*tty_ioctl)(tty_t *tty, unsigned long request, void *arg);
  int (*tty_update)(tty_t *tty, struct termios *termios);
  int (*tty_modem)(tty_t *tty, int command, int arg);
  bool (*tty_isbusy)(tty_t *tty);
};

// tty_modem commands
#define TTY_MODEM_DTR    0x01 // data terminal ready
#define TTY_MODEM_RTS    0x02 // request to send
// tty_modem status bitmasks
#define TTY_MODEM_BM_DSR 0x01 // data set ready
#define TTY_MODEM_BM_CTS 0x02 // clear to send
#define TTY_MODEM_BM_DCD 0x04 // data carrier detect
#define TTY_MODEM_BM_RI  0x08 // ring indicator

#define tty_lock(tty) \
  ({ \
    mtx_lock(&(tty)->lock); \
    bool _locked = true; \
    if ((tty)->flags & TTYF_GONE) { \
      mtx_unlock(&(tty)->lock); \
      _locked = false; \
    } \
    _locked; \
  })
#define tty_unlock(tty) mtx_unlock(&(tty)->lock)
#define tty_assert_locked(tty) mtx_assert(&(tty)->lock, MA_LOCKED)
#define tty_assert_owned(tty) mtx_assert(&(tty)->lock, MA_OWNED)
#define tty_assert_unowned(tty) mtx_assert(&(tty)->lock, MA_NOTOWNED)

tty_t *tty_alloc(struct ttydev_ops *ops, void *data);
void tty_free(tty_t **ttyp);

int tty_open(tty_t *tty);
int tty_close(tty_t *tty);
int tty_configure(tty_t *tty, struct termios *termios, struct winsize *winsize);
int tty_modem(tty_t *tty, int command, int arg);
int tty_ioctl(tty_t *tty, unsigned long request, void *arg);
int tty_wait_cond(tty_t *tty, cond_t *cond);
void tty_signal_cond(tty_t *tty, cond_t *cond);
int tty_signal_pgrp(tty_t *tty, int signal);

void termios_print_debug(struct termios *t);
void winsize_print_debug(struct winsize *ws);

static inline struct termios termios_make_canon(speed_t speed) {
  struct termios t;
  t.c_iflag = ICRNL | IXON | BRKINT;  // input flags: translate CR to NL, enable XON/XOFF flow control
  t.c_oflag = OPOST | ONLCR | XTABS;  // output flags: post-process output, translate NL to CR-NL
  t.c_cflag = CS8 | CREAD | CLOCAL;   // control flags: 8 data bits, enable receiver, ignore modem control
  t.c_lflag = ISIG | ICANON | ECHO | ECHOCTL; // local flags: enable signals, canonical mode, echo input, echo control characters
  ttydisc_fill_cc_default(&t);
  t.__c_ispeed = speed; // default input speed
  t.__c_ospeed = speed; // default output speed
  return t;
}

#endif
