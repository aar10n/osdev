//
// Created by Aaron Gill-Braun on 2023-12-20.
//

#ifndef KERNEL_TTY_H
#define KERNEL_TTY_H

#include <kernel/base.h>
#include <kernel/mutex.h>
#include <kernel/cond.h>

#include <kernel/tty/tty_disc.h>
#include <kernel/tty/tty_queue.h>

#include <abi/termios.h>

struct pgroup;
struct session;

struct ttydev_ops;


typedef struct tty {
  uint32_t flags;             // tty flags
  mtx_t lock;                 // tty lock
  struct tty_inq *inq;        // input queue
  size_t inq_watermark;       // input queue watermark
  struct tty_outq *outq;      // output queue
  size_t outq_watermark;       // input queue watermark

  struct cond	in_wait;	      // input wait queue
  struct cond	out_wait;	      // output wait queue.

  struct termios termios;     // terminal attributes
  struct winsize winsize;     // window size

  struct pgroup *pgrp;        // foreground process group
  struct session *session;    // associated session

  struct ttydev_ops *dev_ops; // device operations
  void *dev_data;             // device-specific data
} tty_t;

// tty flags
#define TTYF_OPENED 0x0001    // tty is opened
#define TTYF_GONE 0x0002      // tty device is gone


struct ttydev_ops {
  int (*tty_open)(tty_t *tty);
  void (*tty_close)(tty_t *tty);
  void (*tty_outwakeup)(tty_t *tty);

  int (*tty_ioctl)(tty_t *tty, unsigned long request, void *arg);
  int (*tty_update)(tty_t *tty, struct termios *termios);
  int (*tty_modem)(tty_t *tty, int command, int arg);
  bool (*tty_isbusy)(tty_t *tty);
};

#define TTY_MODEM_DTR 0x01
#define TTY_MODEM_RTS 0x02

#define tty_lock(tty) mtx_lock(&(tty)->lock)
#define tty_unlock(tty) mtx_unlock(&(tty)->lock)
#define tty_lock_assert(tty, type) mtx_assert(&(tty)->lock, type)
#define tty_assert_locked(tty) mtx_assert(&(tty)->lock, MA_LOCKED)


tty_t *tty_alloc(struct ttydev_ops *ops, void *data);
void tty_free(tty_t **ttyp);



#endif
