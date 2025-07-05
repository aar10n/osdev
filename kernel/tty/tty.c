//
// Created by Aaron Gill-Braun on 2023-12-20.
//

#include <kernel/tty.h>
#include <kernel/device.h>
#include <kernel/mm.h>
#include <kernel/proc.h>

#include <kernel/printf.h>
#include <kernel/panic.h>

#include <bits/ioctl.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("tty: " fmt, ##__VA_ARGS__)
#define EPRINTF(fmt, ...) kprintf("tty: %s: " fmt, __func__, ##__VA_ARGS__)

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

ssize_t tty_dev_read(device_t *dev, size_t off, size_t nmax, kio_t *kio) {
  tty_t *tty = (tty_t *) dev->data;
  if (tty == NULL) {
    EPRINTF("tty device is not initialized\n");
    return -ENODEV; // device is not initialized
  } else if (off > 0) {
    EPRINTF("tty device does not support offset: %zu\n", off);
    return -EINVAL; // invalid offset
  }

  if (!tty_lock(tty)) {
    EPRINTF("tty device is gone\n");
    return -ENXIO; // device is gone
  }

  ssize_t res = ttydisc_read(tty, kio);
  tty_unlock(tty);
  return res;
}

ssize_t tty_dev_write(device_t *dev, size_t off, size_t nmax, kio_t *kio) {
  tty_t *tty = (tty_t *) dev->data;
  if (tty == NULL) {
    EPRINTF("tty device is not initialized\n");
    return -ENODEV; // device is not initialized
  } else if (off > 0) {
    EPRINTF("tty device does not support offset: %zu\n", off);
    return -EINVAL; // offset is not supported
  }

  if (!tty_lock(tty)) {
    EPRINTF("tty device is gone\n");
    return -ENXIO; // device is gone
  }

  ssize_t res = ttydisc_write(tty, kio);
  tty_unlock(tty);
  return res;
}

int tty_dev_ioctl(device_t *dev, unsigned long cmd, void *arg) {
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

static struct device_ops tty_dev_ops = {
  .d_open = tty_dev_open,
  .d_close = tty_dev_close,
  .d_read = tty_dev_read,
  .d_write = tty_dev_write,
  .d_ioctl = tty_dev_ioctl
};

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
  cond_init(&tty->in_wait, "tty_in_wait");
  cond_init(&tty->out_wait, "tty_out_wait");
  cond_init(&tty->dcd_wait, "tty_dcd_wait");
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
      if (vm_validate_user_ptr((uintptr_t) arg, /*write=*/true) < 0) {
        EPRINTF("TCGETS ioctl requires a valid argument\n");
        return -EINVAL; // invalid argument
      }
      struct termios *termios = (struct termios *) arg;
      *termios = tty->termios;
      return 0; // success
    }
    case TCSETS: {
      if (vm_validate_user_ptr((uintptr_t) arg, /*write=*/false) < 0) {
        EPRINTF("TCSETS ioctl requires a valid argument\n");
        return -EINVAL; // invalid argument
      }
      // configure the terminal attributes
      struct termios *termios = (struct termios *) arg;
      return tty_configure(tty, termios, NULL);
    }
    case TCSETSW: {
      if (vm_validate_user_ptr((uintptr_t) arg, /*write=*/false) < 0) {
        EPRINTF("TCSETSW ioctl requires a valid argument\n");
        return -EINVAL; // invalid argument
      }

      // drain the output queue
      tty->dev_ops->tty_outwakeup(tty);

      // configure the terminal attributes
      struct termios *termios = (struct termios *) arg;
      return tty_configure(tty, termios, NULL);
    }
    case TCSETSF: {
      if (vm_validate_user_ptr((uintptr_t) arg, /*write=*/false) < 0) {
        EPRINTF("TCSETSF ioctl requires a valid argument\n");
        return -EINVAL; // invalid argument
      }

      // drain the output queue
      tty->dev_ops->tty_outwakeup(tty);
      // flush the input queue
      ttyinq_flush(tty->inq);

      // configure the terminal attributes
      struct termios *termios = (struct termios *) arg;
      return tty_configure(tty, termios, NULL);
    }
    // Locking the termios structure
    case TIOCGLCKTRMIOS:
      todo("TIOCGLCKTRMIOS ioctl not implemented");
    case TIOCSLCKTRMIOS:
      todo("TIOCSLCKTRMIOS ioctl not implemented");
    // Get and set window size
    case TIOCGWINSZ: {
      if (vm_validate_user_ptr((uintptr_t) arg, /*write=*/true) < 0) {
        EPRINTF("TIOCGWINSZ ioctl requires a valid argument\n");
        return -EINVAL; // invalid argument
      }
      struct winsize *ws = (struct winsize *) arg;
      ws->ws_row = tty->winsize.ws_row;
      ws->ws_col = tty->winsize.ws_col;
      ws->ws_xpixel = tty->winsize.ws_xpixel;
      ws->ws_ypixel = tty->winsize.ws_ypixel;
      return 0; // success
    }
    case TIOCSWINSZ: {
      if (vm_validate_user_ptr((uintptr_t) arg, /*write=*/false) < 0) {
        EPRINTF("TIOCSWINSZ ioctl requires a valid argument\n");
        return -EINVAL; // invalid argument
      }
      struct winsize *ws = (struct winsize *) arg;
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
      if (vm_validate_user_ptr((uintptr_t) arg, /*write=*/true) < 0) {
        EPRINTF("FIONREAD ioctl requires a valid argument\n");
        return -EINVAL; // invalid argument
      }
      size_t bytes = ttyinq_canonbytes(tty->inq);
      ASSERT(bytes <= INT_MAX);
      *((int *)arg) = (int) bytes;
      return 0; // success
    }
    case TIOCOUTQ: {
      if (vm_validate_user_ptr((uintptr_t) arg, /*write=*/true) < 0) {
        EPRINTF("TIOCOUTQ ioctl requires a valid argument\n");
        return -EINVAL; // invalid argument
      }
      size_t bytes = ttyoutq_bytes(tty->outq);
      ASSERT(bytes <= INT_MAX);
      *((int *)arg) = (int) bytes;
      return 0; // success
    }
    case TCFLSH: {
      if (vm_validate_user_ptr((uintptr_t) arg, /*write=*/false) < 0) {
        EPRINTF("TCFLSH ioctl requires a valid argument\n");
        return -EINVAL; // invalid argument
      }
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
      if (vm_validate_user_ptr((uintptr_t) arg, /*write=*/false) < 0) {
        EPRINTF("TIOCSTI ioctl requires a valid argument\n");
        return -EINVAL; // invalid argument
      }
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
      pr_lock(proc);
      if (!proc_is_sess_leader(proc)) {
        EPRINTF("process {:pr} is not a session leader\n", proc);
        pr_unlock(proc);
        return -EPERM; // operation not permitted
      }

      session_t *sess = proc->group->session;
      int res = session_leader_ctty(sess, tty);
      pr_unlock(proc);
      return res;
    }
    case TIOCNOTTY: {
      pr_lock(proc);
      if (!proc_is_sess_leader(proc)) {
        EPRINTF("process {:pr} is not a session leader\n", proc);
        pr_unlock(proc);
        return -EPERM; // operation not permitted
      }

      session_t *sess = proc->group->session;
      int res = session_leader_ctty(sess, NULL);
      pr_unlock(proc);
      return res;
    }
    // Process group and session ID
    case TIOCGPGRP: {
      if (vm_validate_user_ptr((uintptr_t) arg, /*write=*/true) < 0) {
        EPRINTF("TIOCGPGRP ioctl requires a valid argument\n");
        return -EINVAL; // invalid argument
      }
      ASSERT(tty->pgrp != NULL);
      *((pid_t *)arg) = tty->pgrp->pgid;
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
  cond_broadcast(cond);
}

int tty_signal_pgrp(tty_t *tty, int signal) {
  if (!tty_lock(tty)) {
    EPRINTF("tty device is gone\n");
    return -ENXIO; // device is gone
  }

  union sigval si_value = {0};
  int res = pgrp_signal(tty->pgrp, signal, SI_KERNEL, si_value);
  tty_unlock(tty);
  if (res < 0) {
    EPRINTF("failed to signal pgrp: {:err}\n", res);
  }
  return res;
}


static void tty_static_init() {
  register_device_ops("serial", &tty_dev_ops);
}
STATIC_INIT(tty_static_init);
