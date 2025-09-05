//
// Created by Aaron Gill-Braun on 2023-12-20.
//

#include <kernel/tty.h>
#include <kernel/device.h>
#include <kernel/kevent.h>
#include <kernel/mm.h>
#include <kernel/proc.h>

#include <kernel/printf.h>
#include <kernel/panic.h>

#include <abi/ioctl.h>
#include <bits/fcntl.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("tty: " fmt, ##__VA_ARGS__)
#define EPRINTF(fmt, ...) kprintf("tty: %s: " fmt, __func__, ##__VA_ARGS__)
#define DEBUG(x)
//#define DEBUG(x) x

//
// MARK: Public API
//

tty_t *tty_alloc(struct ttydev_ops *ops, void *data) {
  tty_t *tty = kmallocz(sizeof(tty_t));
  tty->inq = ttyinq_alloc();
  tty->outq = ttyoutq_alloc();
  tty->dev_ops = ops;
  tty->dev_data = data;

  mtx_init(&tty->lock, MTX_RECURSIVE, "tty_lock");
  cond_init(&tty->in_data_cond, "tty_in_data");
  cond_init(&tty->out_data_cond, "tty_out_data");
  cond_init(&tty->outready_cond, "tty_outready");
  cond_init(&tty->dcd_cond, "tty_dcd");
  knlist_init(&tty->knlist, &tty->lock.lo);
  return tty;
}

void tty_free(tty_t **ttyp) {
  tty_t *tty = moveptr(*ttyp);
  if (tty == NULL) {
    return;
  }
  todo();
}

int tty_open(tty_t *tty) {
  tty_assert_owned(tty);

  int res;
  if (tty->flags & TTYF_OPENED) {
    EPRINTF("tty is already opened\n");
    return -EBUSY; // device is already opened
  }

  ttydisc_open(tty);
  if ((res = tty->dev_ops->tty_open(tty)) < 0) {
    EPRINTF("failed to open ttydev: {:err}\n", res);
    return res;
  }
  tty->flags |= TTYF_OPENED;
  return 0;
}

int tty_close(tty_t *tty) {
  tty_assert_owned(tty);
  if (!(tty->flags & TTYF_OPENED)) {
    EPRINTF("tty is not opened\n");
    return -ENODEV; // device is not opened
  }

  tty->dev_ops->tty_close(tty);
  ttydisc_close(tty);
  tty->flags &= ~TTYF_OPENED;
  return 0;
}

int tty_configure(tty_t *tty, struct termios *termios, struct winsize *winsize) {
  tty_assert_owned(tty);
  if (termios == NULL && winsize == NULL) {
    EPRINTF("no configuration provided\n");
    return -EINVAL;
  } else if (termios != NULL && termios->__c_ospeed == 0) {
    EPRINTF("invalid baud rate: %d\n", termios->__c_ospeed);
    return -EINVAL;
  } else if (winsize != NULL && (winsize->ws_row == 0 || winsize->ws_col == 0)) {
    EPRINTF("invalid window size: %ux%u\n", winsize->ws_row, winsize->ws_col);
    return -EINVAL;
  }

  if (termios != NULL) {
    speed_t old_speed = tty->termios.__c_ospeed;
    int res = tty->dev_ops->tty_update(tty, termios);
    if (res < 0) {
      EPRINTF("failed to update tty: {:err}\n", res);
      return res;
    }

    tty->termios = *termios;
    if (tty->termios.__c_ospeed != old_speed) {
      // speed has changed, resize out buffers accordingly
      size_t new_size = tty->termios.__c_ospeed / 10;
      DPRINTF("resizing input and output queues to %zu bytes\n", new_size);
      if (ttyinq_setsize(tty->inq, new_size) < 0) {
        EPRINTF("failed to resize input queue to %zu bytes\n", new_size);
        return -ENOMEM; // memory allocation error
      }
      if (ttyoutq_setsize(tty->outq, new_size) < 0) {
        EPRINTF("failed to resize output queue to %zu bytes\n", new_size);
        return -ENOMEM; // memory allocation error
      }

      tty->column = 0;
    }
  }

  if (winsize != NULL) {
    tty->winsize = *winsize;
    // todo: reprint?
  }
  return 0;
}

int tty_modem(tty_t *tty, int command, int arg) {
  tty_assert_owned(tty);
  if (tty->dev_ops == NULL || tty->dev_ops->tty_modem == NULL) {
    EPRINTF("tty device does not support modem control\n");
    return -ENOSYS; // operation not supported
  }

  return tty->dev_ops->tty_modem(tty, command, arg);
}

int tty_ioctl(tty_t *tty, unsigned long request, void *arg) {
  tty_assert_owned(tty);
  proc_t *proc = curproc;
  switch (request) {
    // Get and set terminal attributes
    case TCGETS: {
      if (vm_validate_ptr((uintptr_t) arg, /*write=*/true) < 0) {
        EPRINTF("TCGETS ioctl requires a valid argument\n");
        return -EINVAL; // invalid argument
      }

      struct termios *termios = (struct termios *) arg;
      DPRINTF("TCGETS ioctl\n");
      DEBUG(DPRINTF("current termios: "));
      DEBUG(termios_print_debug(&tty->termios));

      *termios = tty->termios;
      return 0; // success
    }
    case TCSETS: {
      if (vm_validate_ptr((uintptr_t) arg, /*write=*/false) < 0) {
        EPRINTF("TCSETS ioctl requires a valid argument\n");
        return -EINVAL; // invalid argument
      }

      struct termios *termios = (struct termios *) arg;
      DPRINTF("TCSETS ioctl\n");
      DEBUG(DPRINTF("new termios: "));
      DEBUG(termios_print_debug(termios));

      // configure the terminal attributes
      return tty_configure(tty, termios, NULL);
    }
    case TCSETSW: {
      if (vm_validate_ptr((uintptr_t) arg, /*write=*/false) < 0) {
        EPRINTF("TCSETSW ioctl requires a valid argument\n");
        return -EINVAL; // invalid argument
      }

      // drain the output queue
      tty->dev_ops->tty_outwakeup(tty);

      struct termios *termios = (struct termios *) arg;
      DPRINTF("TCSETSW ioctl\n");
      DPRINTF("new termios: ");
      DEBUG(termios_print_debug(termios));

      // configure the terminal attributes
      return tty_configure(tty, termios, NULL);
    }
    case TCSETSF: {
      if (vm_validate_ptr((uintptr_t) arg, /*write=*/false) < 0) {
        EPRINTF("TCSETSF ioctl requires a valid argument\n");
        return -EINVAL; // invalid argument
      }

      // drain the output queue
      tty->dev_ops->tty_outwakeup(tty);
      // flush the input queue
      ttyinq_flush(tty->inq);

      struct termios *termios = (struct termios *) arg;
      DPRINTF("TCSETSW ioctl\n");
      DPRINTF("new termios: ");
      DEBUG(termios_print_debug(termios);)

      // configure the terminal attributes
      return tty_configure(tty, termios, NULL);
    }
    // Locking the termios structure
    case TIOCGLCKTRMIOS:
      todo("TIOCGLCKTRMIOS ioctl not implemented");
    case TIOCSLCKTRMIOS:
      todo("TIOCSLCKTRMIOS ioctl not implemented");
    // Get and set window size
    case TIOCGWINSZ: {
      if (vm_validate_ptr((uintptr_t) arg, /*write=*/true) < 0) {
        EPRINTF("TIOCGWINSZ ioctl requires a valid argument\n");
        return -EINVAL; // invalid argument
      }

      struct winsize *ws = (struct winsize *) arg;
      DPRINTF("TIOCGWINSZ ioctl\n");
      DEBUG(DPRINTF("current window size: "));
      DEBUG(winsize_print_debug(&tty->winsize));

      ws->ws_row = tty->winsize.ws_row;
      ws->ws_col = tty->winsize.ws_col;
      ws->ws_xpixel = tty->winsize.ws_xpixel;
      ws->ws_ypixel = tty->winsize.ws_ypixel;
      return 0; // success
    }
    case TIOCSWINSZ: {
      if (vm_validate_ptr((uintptr_t) arg, /*write=*/false) < 0) {
        EPRINTF("TIOCSWINSZ ioctl requires a valid argument\n");
        return -EINVAL; // invalid argument
      }

      struct winsize *ws = (struct winsize *) arg;
      DPRINTF("TIOCSWINSZ ioctl\n");
      DEBUG(DPRINTF("new window size: "));
      DEBUG(winsize_print_debug(ws));

      if (ws->ws_row == 0 || ws->ws_col == 0) {
        EPRINTF("invalid window size: %ux%u\n", ws->ws_row, ws->ws_col);
        return -EINVAL; // invalid window size
      }
      tty->winsize = *ws;
      return 0; // success
    }
    // Sending a break
    case TCSBRK:
      todo("TCSBRK ioctl not implemented");
    case TIOCSBRK:
      todo("TIOCSBRK ioctl not implemented");
    case TIOCCBRK:
      todo("TIOCCBRK ioctl not implemented");
    // Software flow control
    case TCXONC:
      todo("TCXONC ioctl not implemented");
    // Buffer count and flushing
    case TIOCINQ: {
      if (vm_validate_ptr((uintptr_t) arg, /*write=*/true) < 0) {
        EPRINTF("FIONREAD ioctl requires a valid argument\n");
        return -EINVAL; // invalid argument
      }
      DPRINTF("TIOCINQ ioctl\n");
      size_t bytes = ttyinq_canonbytes(tty->inq);
      ASSERT(bytes <= INT_MAX);
      *((int *)arg) = (int) bytes;
      DPRINTF("TIOCINQ ioctl: bytes=%d\n", *((int *)arg));
      return 0; // success
    }
    case TIOCOUTQ: {
      if (vm_validate_ptr((uintptr_t) arg, /*write=*/true) < 0) {
        EPRINTF("TIOCOUTQ ioctl requires a valid argument\n");
        return -EINVAL; // invalid argument
      }
      DPRINTF("TIOCOUTQ ioctl\n");
      size_t bytes = ttyoutq_bytes(tty->outq);
      ASSERT(bytes <= INT_MAX);
      *((int *)arg) = (int) bytes;
      DPRINTF("TIOCOUTQ ioctl: bytes=%d\n", *((int *)arg));
      return 0; // success
    }
    case TCFLSH: {
      if (vm_validate_ptr((uintptr_t) arg, /*write=*/false) < 0) {
        EPRINTF("TCFLSH ioctl requires a valid argument\n");
        return -EINVAL; // invalid argument
      }
      DPRINTF("TCFLSH ioctl\n");
      int queue = *((int *)arg);
      if (queue == TCIFLUSH) {
        ttyinq_flush(tty->inq);
      } else if (queue == TCOFLUSH) {
        ttyoutq_flush(tty->outq);
      } else if (queue == TCIOFLUSH) {
        ttyinq_flush(tty->inq);
        ttyoutq_flush(tty->outq);
      } else {
        EPRINTF("invalid queue for TCFLSH ioctl: %d\n", queue);
        return -EINVAL; // invalid argument
      }
      return 0; // success
    }
    // Faking input
    case TIOCSTI: {
      if (vm_validate_ptr((uintptr_t) arg, /*write=*/false) < 0) {
        EPRINTF("TIOCSTI ioctl requires a valid argument\n");
        return -EINVAL; // invalid argument
      }

      DPRINTF("TIOCSTI ioctl\n");
      DPRINTF("  '{:#c}'\n", *((char *)arg));

      int res;
      char ch = *((char *)arg);
      if ((res = ttydisc_rint(tty, ch, 0)) < 0) {
        EPRINTF("failed to write character to input queue\n");
        return res;
      }
      return 0; // success
    }
    // Redirecting console output
    case TIOCCONS:
      todo("TIOCCONS ioctl not implemented");
    // Controlling terminal
    case TIOCSCTTY: {
      DPRINTF("TIOCSCTTY ioctl\n");
      pr_lock(proc);
      if (!proc_is_sess_leader(proc)) {
        EPRINTF("process {:pr} is not a session leader\n", proc);
        pr_unlock(proc);
        return -EPERM; // operation not permitted
      }

      session_t *sess = proc->group->session;
      int res = session_leader_ctty(sess, tty);
      pr_unlock(proc);
      DPRINTF("TIOCSCTTY ioctl res={:err}\n", res);
      return res;
    }
    case TIOCNOTTY: {
      DPRINTF("TIOCNOTTY ioctl\n");
      pr_lock(proc);
      if (!proc_is_sess_leader(proc)) {
        EPRINTF("process {:pr} is not a session leader\n", proc);
        pr_unlock(proc);
        return -EPERM; // operation not permitted
      }

      session_t *sess = proc->group->session;
      int res = session_leader_ctty(sess, NULL);
      pr_unlock(proc);
      DPRINTF("TIOCNOTTY ioctl res={:err}\n", res);
      return res;
    }
    // Process group and session ID
    case TIOCGPGRP: {
      if (vm_validate_ptr((uintptr_t) arg, /*write=*/true) < 0) {
        EPRINTF("TIOCGPGRP ioctl requires a valid argument\n");
        return -EINVAL; // invalid argument
      }
      ASSERT(tty->pgrp != NULL);
      DPRINTF("TIOCGPGRP ioctl\n");
      *((pid_t *)arg) = tty->pgrp->pgid;
      DPRINTF("TIOCGPGRP ioctl pgid=%d\n", tty->pgrp->pgid);
      return 0; // success
    }
    case TIOCSPGRP:
      todo("TIOCSPGRP ioctl not implemented");
    case TIOCGSID:
      todo("TIOCGSID ioctl not implemented");
    // Exclusive mode
    case TIOCEXCL:
      todo("TIOCEXCL ioctl not implemented");
    case TIOCNXCL:
      todo("TIOCNXCL ioctl not implemented");
    // Line discipline
    case TIOCGETD:
    case TIOCSETD:
      return -ENOTTY;
    default:
      break;
  }

  // other ioctls may be handled by the device driver
  if (tty->dev_ops == NULL || tty->dev_ops->tty_ioctl == NULL) {
    EPRINTF("tty device does not support ioctl\n");
    return -ENOTSUP; // operation not supported
  }
  return tty->dev_ops->tty_ioctl(tty, request, arg);
}

int tty_wait_cond(tty_t *tty, cond_t *cond) {
  tty_assert_owned(tty);

  cond_wait(cond, &tty->lock);
  // check tty flags again after wakeup
  if (tty->flags & TTYF_GONE) {
    EPRINTF("tty device is gone\n");
    return -ENXIO; // device is gone
  }
  return 0;
}

void tty_signal_cond(tty_t *tty, cond_t *cond) {
  tty_assert_owned(tty);
  if (cond == &tty->in_data_cond) {
    // we only want to signal the input wait condition if there is available
    // data to read (i.e. a full line if ICANON is set, or any data if raw mode)
    if (ttydisc_bytesavail(tty) == 0) {
      return;
    }
  }

  cond_broadcast(cond);

  if (cond == &tty->in_data_cond) {
    // if we signaled the input wait condition, we also want to update any knotes
    // that are attached to the tty device
    DPRINTF("activating knotes for tty device\n");
    knlist_activate_notes(&tty->knlist, NOTE_READ);
  }
}

int tty_signal_pgrp(tty_t *tty, int signal) {
  if (!tty_lock(tty)) {
    EPRINTF("tty device is gone\n");
    return -ENXIO; // device is gone
  }

  pgroup_t *pgrp = tty->pgrp;
  if (pgrp == NULL) {
    EPRINTF("tty device is not associated with a process group\n");
    tty_unlock(tty);
    return -ENOTTY; // not a tty
  }

  pgrp_lock(pgrp);
  int res = pgrp_signal(pgrp, &(siginfo_t){.si_signo = signal});
  pgrp_unlock(pgrp);
  tty_unlock(tty);
  if (res < 0) {
    EPRINTF("failed to signal pgrp: {:err}\n", res);
  }
  return res;
}

//
// MARK: Device API
//

int tty_dev_open(device_t *dev, int flags) {
  tty_t *tty = (tty_t *) dev->data;
  if (tty == NULL) {
    EPRINTF("tty device is not initialized\n");
    return -ENODEV; // device is not initialized
  }

  if (!tty_lock(tty)) {
    EPRINTF("tty device is gone\n");
    return -ENXIO; // device is gone
  }

  if (flags & O_NONBLOCK) {
    tty->flags |= TTYF_NONBLOCK;
  } else {
    tty->flags &= ~TTYF_NONBLOCK;
  }

  int res = tty_open(tty);
  tty_unlock(tty);
  return res;
}

int tty_dev_close(device_t *dev) {
  tty_t *tty = (tty_t *) dev->data;
  if (tty == NULL) {
    EPRINTF("tty device is not initialized\n");
    return -ENODEV; // device is not initialized
  }

  if (!tty_lock(tty)) {
    EPRINTF("tty device is gone\n");
    return -ENXIO; // device is gone
  }

  int res = tty_close(tty);
  tty_unlock(tty);
  return res;
}

ssize_t tty_dev_read(device_t *dev, _unused size_t off, size_t nmax, kio_t *kio) {
  tty_t *tty = (tty_t *) dev->data;
  if (tty == NULL) {
    EPRINTF("tty device is not initialized\n");
    return -ENODEV; // device is not initialized
  }

  if (!tty_lock(tty)) {
    EPRINTF("tty device is gone\n");
    return -ENXIO; // device is gone
  }

  ssize_t res = ttydisc_read(tty, kio);
  tty_unlock(tty);
  return res;
}

ssize_t tty_dev_write(device_t *dev, _unused size_t off, size_t nmax, kio_t *kio) {
  tty_t *tty = (tty_t *) dev->data;
  if (tty == NULL) {
    EPRINTF("tty device is not initialized\n");
    return -ENODEV; // device is not initialized
  }

  if (!tty_lock(tty)) {
    EPRINTF("tty device is gone\n");
    return -ENXIO; // device is gone
  }

  ssize_t res = ttydisc_write(tty, kio);
  tty_unlock(tty);
  return res;
}

int tty_dev_ioctl(device_t *dev, unsigned int cmd, void *arg) {
  tty_t *tty = (tty_t *) dev->data;
  if (tty == NULL) {
    EPRINTF("tty device is not initialized\n");
    return -ENODEV; // device is not initialized
  }

  if (!tty_lock(tty)) {
    EPRINTF("tty device is gone\n");
    return -ENXIO; // device is gone
  }

  int res = tty_ioctl(tty, cmd, arg);
  tty_unlock(tty);
  return res;
}

int tty_dev_kqattach(device_t *dev, knote_t *kn) {
  tty_t *tty = (tty_t *) dev->data;
  ASSERT(tty != NULL);
  kn->filt_ops_data = tty;
  knote_add_list(kn, &tty->knlist);
  return 0;
}

void tty_dev_kqdetach(device_t *dev, knote_t *kn) {
  tty_t *tty = (tty_t *) dev->data;
  ASSERT(tty != NULL);
  knote_remove_list(kn);
  kn->filt_ops_data = NULL;
}

int tty_dev_kqevent(device_t *dev, knote_t *kn) {
  tty_t *tty = (tty_t *) dev->data;
  ASSERT(tty != NULL);
  ASSERT(kn->event.filter == EVFILT_READ);
  // should only be called for read events

  int report;
  size_t nbytes = ttydisc_bytesavail(tty);
  if (nbytes > 0) {
    // return the number of bytes available
    kn->event.data = (intptr_t) nbytes;
    report = 1;
  } else {
    report = 0;
  }
  return report;
}

static struct device_ops tty_dev_ops = {
  .d_open = tty_dev_open,
  .d_close = tty_dev_close,
  .d_read = tty_dev_read,
  .d_write = tty_dev_write,
  .d_ioctl = tty_dev_ioctl,

  .d_kqattach = tty_dev_kqattach,
  .d_kqdetach = tty_dev_kqdetach,
  .d_kqevent = tty_dev_kqevent,
};

static void tty_static_init() {
  register_device_ops("serial", &tty_dev_ops);
}
STATIC_INIT(tty_static_init);

//
// MARK: Debugging
//

const char *termios_speed_str(speed_t s) {
  switch (s) {
    case B0: return "0";
    case B50: return "50";
    case B75: return "75";
    case B110: return "110";
    case B134: return "134";
    case B150: return "150";
    case B200: return "200";
    case B300: return "300";
    case B600: return "600";
    case B1200: return "1200";
    case B1800: return "1800";
    case B2400: return "2400";
    case B4800: return "4800";
    case B9600: return "9600";
    case B19200: return "19200";
    case B38400: return "38400";
    case B57600: return "57600";
    case B115200: return "115200";
    case B230400: return "230400";
    case B460800: return "460800";
    case B500000: return "500000";
    case B576000: return "576000";
    case B921600: return "921600";
    case B1000000: return "1000000";
    case B1152000: return "1152000";
    case B1500000: return "1500000";
    case B2000000: return "2000000";
    case B2500000: return "2500000";
    case B3000000: return "3000000";
    case B3500000: return "3500000";
    case B4000000: return "4000000";
    default: return "unknown";
  }
}

void termios_print_debug(struct termios *t) {
  if (!t) {
    kprintf("termios is NULL\n");
    return;
  }

  kprintf("termios at %p:\n", t);

  // input flags
  kprintf("  c_iflag: 0x%08x", t->c_iflag);
  if (t->c_iflag & IGNBRK) kprintf(" IGNBRK");
  if (t->c_iflag & BRKINT) kprintf(" BRKINT");
  if (t->c_iflag & IGNPAR) kprintf(" IGNPAR");
  if (t->c_iflag & PARMRK) kprintf(" PARMRK");
  if (t->c_iflag & INPCK) kprintf(" INPCK");
  if (t->c_iflag & ISTRIP) kprintf(" ISTRIP");
  if (t->c_iflag & INLCR) kprintf(" INLCR");
  if (t->c_iflag & IGNCR) kprintf(" IGNCR");
  if (t->c_iflag & ICRNL) kprintf(" ICRNL");
  if (t->c_iflag & IUCLC) kprintf(" IUCLC");
  if (t->c_iflag & IXON) kprintf(" IXON");
  if (t->c_iflag & IXANY) kprintf(" IXANY");
  if (t->c_iflag & IXOFF) kprintf(" IXOFF");
  if (t->c_iflag & IMAXBEL) kprintf(" IMAXBEL");
  if (t->c_iflag & IUTF8) kprintf(" IUTF8");
  kprintf("\n");

  // output flags
  kprintf("  c_oflag: 0x%08x", t->c_oflag);
  if (t->c_oflag & OPOST) kprintf(" OPOST");
  if (t->c_oflag & OLCUC) kprintf(" OLCUC");
  if (t->c_oflag & ONLCR) kprintf(" ONLCR");
  if (t->c_oflag & OCRNL) kprintf(" OCRNL");
  if (t->c_oflag & ONOCR) kprintf(" ONOCR");
  if (t->c_oflag & ONLRET) kprintf(" ONLRET");
  if (t->c_oflag & OFILL) kprintf(" OFILL");
  if (t->c_oflag & OFDEL) kprintf(" OFDEL");
  if (t->c_oflag & NLDLY) kprintf(" NLDLY(%d)", (t->c_oflag & NLDLY) >> 8);
  if (t->c_oflag & CRDLY) kprintf(" CRDLY(%d)", (t->c_oflag & CRDLY) >> 9);
  if (t->c_oflag & TABDLY) kprintf(" TABDLY(%d)", (t->c_oflag & TABDLY) >> 11);
  if (t->c_oflag & BSDLY) kprintf(" BSDLY(%d)", (t->c_oflag & BSDLY) >> 13);
  if (t->c_oflag & VTDLY) kprintf(" VTDLY");
  if (t->c_oflag & FFDLY) kprintf(" FFDLY");
  kprintf("\n");

  // control flags
  kprintf("  c_cflag: 0x%08x", t->c_cflag);
  switch (t->c_cflag & CSIZE) {
    case CS5: kprintf(" CS5"); break;
    case CS6: kprintf(" CS6"); break;
    case CS7: kprintf(" CS7"); break;
    case CS8: kprintf(" CS8"); break;
    default: kprintf(" CS?"); break;
  }
  if (t->c_cflag & CSTOPB) kprintf(" CSTOPB");
  if (t->c_cflag & CREAD) kprintf(" CREAD");
  if (t->c_cflag & PARENB) kprintf(" PARENB");
  if (t->c_cflag & PARODD) kprintf(" PARODD");
  if (t->c_cflag & HUPCL) kprintf(" HUPCL");
  if (t->c_cflag & CLOCAL) kprintf(" CLOCAL");
  if (t->c_cflag & CRTSCTS) kprintf(" CRTSCTS");
  kprintf("\n");

  // local flags
  kprintf("  c_lflag: 0x%08x", t->c_lflag);
  if (t->c_lflag & ISIG) kprintf(" ISIG");
  if (t->c_lflag & ICANON) kprintf(" ICANON");
  if (t->c_lflag & XCASE) kprintf(" XCASE");
  if (t->c_lflag & ECHO) kprintf(" ECHO");
  if (t->c_lflag & ECHOE) kprintf(" ECHOE");
  if (t->c_lflag & ECHOK) kprintf(" ECHOK");
  if (t->c_lflag & ECHONL) kprintf(" ECHONL");
  if (t->c_lflag & NOFLSH) kprintf(" NOFLSH");
  if (t->c_lflag & TOSTOP) kprintf(" TOSTOP");
  if (t->c_lflag & ECHOCTL) kprintf(" ECHOCTL");
  if (t->c_lflag & ECHOPRT) kprintf(" ECHOPRT");
  if (t->c_lflag & ECHOKE) kprintf(" ECHOKE");
  if (t->c_lflag & FLUSHO) kprintf(" FLUSHO");
  if (t->c_lflag & PENDIN) kprintf(" PENDIN");
  if (t->c_lflag & IEXTEN) kprintf(" IEXTEN");
  if (t->c_lflag & EXTPROC) kprintf(" EXTPROC");
  kprintf("\n");

  // control characters
  kprintf("  c_cc: ");

  // special control chars
  kprintf("VINTR='{:#c}' VQUIT='{:#c}' VERASE='{:#c}' VKILL='{:#c}' VEOF='{:#c}'\n",
          t->c_cc[VINTR], t->c_cc[VQUIT], t->c_cc[VERASE], t->c_cc[VKILL], t->c_cc[VEOF]);
  kprintf("        VSTART='{:#c}' VSTOP='{:#c}' VSUSP='{:#c}' VEOL='{:#c}' VEOL2='{:#c}'\n",
          t->c_cc[VSTART], t->c_cc[VSTOP], t->c_cc[VSUSP], t->c_cc[VEOL], t->c_cc[VEOL2]);
  kprintf("        VREPRINT='{:#c}' VDISCARD='{:#c}' VWERASE='{:#c}' VLNEXT='{:#c}'\n",
          t->c_cc[VREPRINT], t->c_cc[VDISCARD], t->c_cc[VWERASE], t->c_cc[VLNEXT]);
  kprintf("        VTIME=%d VMIN=%d\n",
          t->c_cc[VTIME], t->c_cc[VMIN]);

  kprintf("  ispeed: %s (%d)\n", termios_speed_str(t->__c_ispeed), t->__c_ispeed);
  kprintf("  ospeed: %s (%d)\n", termios_speed_str(t->__c_ospeed), t->__c_ospeed);
}

void winsize_print_debug(struct winsize *ws) {
  if (!ws) {
    kprintf("winsize is NULL\n");
    return;
  }

  kprintf("winsize at %p:\n", ws);
  kprintf("  rows: %u, cols: %u\n", ws->ws_row, ws->ws_col);
  kprintf("  xpixel: %u, ypixel: %u\n", ws->ws_xpixel, ws->ws_ypixel);

  // calculate pixel size per character if possible
  if (ws->ws_row > 0 && ws->ws_col > 0 && ws->ws_xpixel > 0 && ws->ws_ypixel > 0) {
    kprintf("  char size: %ux%u pixels\n",
            ws->ws_xpixel / ws->ws_col,
            ws->ws_ypixel / ws->ws_row);
  }
}
