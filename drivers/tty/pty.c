//
// Created by Aaron Gill-Braun on 2026-03-04.
//

#include <kernel/pty.h>
#include <kernel/tty.h>
#include <kernel/device.h>
#include <kernel/kevent.h>
#include <kernel/mm.h>
#include <kernel/proc.h>
#include <kernel/signal.h>
#include <kernel/fs.h>

#include <kernel/vfs_types.h>
#include <kernel/vfs/file.h>
#include <kernel/vfs/vnode.h>

#include <kernel/printf.h>
#include <kernel/panic.h>

#include <fs/devfs/devfs.h>

#include <abi/ioctl.h>
#include <bits/fcntl.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("pty: " fmt, ##__VA_ARGS__)
#define EPRINTF(fmt, ...) kprintf("pty: %s: " fmt, __func__, ##__VA_ARGS__)

// =========================================================================
// Global state
// =========================================================================

static pty_t *pty_table[MAX_PTYS];
static mtx_t pty_table_lock;

// =========================================================================
// PTY ttydev_ops (slave side callbacks)
// =========================================================================

static int pty_tty_open(tty_t *tty) {
  pty_t *pty = tty->dev_data;
  if (pty->flags & PTYF_LOCKED) {
    EPRINTF("slave is locked\n");
    return -EIO;
  }
  tty->flags |= TTYF_DCDRDY;
  return 0;
}

static void pty_tty_close(tty_t *tty) {
  pty_t *pty = tty->dev_data;
  if (!(pty->flags & PTYF_MASTER_CLOSED) && tty->pgrp) {
    pgroup_t *pgrp = tty->pgrp;
    pgrp_lock(pgrp);
    pgrp_signal(pgrp, &(siginfo_t){.si_signo = SIGHUP});
    pgrp_unlock(pgrp);
  }
}

static void pty_tty_outwakeup(tty_t *tty) {
  pty_t *pty = tty->dev_data;
  cond_broadcast(&tty->out_data_cond);
  knlist_activate_notes(&pty->master_knlist, NOTE_READ);
}

static int pty_tty_ioctl(tty_t *tty, unsigned long request, void *arg) {
  return -ENOTSUP;
}

static int pty_tty_update(tty_t *tty, struct termios *termios) {
  return 0;
}

static int pty_tty_modem(tty_t *tty, int command, int arg) {
  if (command == 0 && arg == 0) {
    return TTY_MODEM_BM_DCD | TTY_MODEM_BM_CTS | TTY_MODEM_BM_DSR;
  }
  return 0;
}

static bool pty_tty_isbusy(tty_t *tty) {
  return false;
}

static struct ttydev_ops pty_ttydev_ops = {
  .tty_open = pty_tty_open,
  .tty_close = pty_tty_close,
  .tty_outwakeup = pty_tty_outwakeup,
  .tty_ioctl = pty_tty_ioctl,
  .tty_update = pty_tty_update,
  .tty_modem = pty_tty_modem,
  .tty_isbusy = pty_tty_isbusy,
};

// =========================================================================
// ptmx file_ops (master side)
// =========================================================================

static int ptmx_f_open(file_t *file, int flags) {
  f_lock_assert(file, LA_OWNED);

  // find and reserve a free pty index (minor 0 is ptmx, slaves start at 1)
  static pty_t pty_reserved; // sentinel value for reserving slots
  mtx_lock(&pty_table_lock);
  int index = -1;
  for (int i = 1; i < MAX_PTYS; i++) {
    if (pty_table[i] == NULL) {
      pty_table[i] = &pty_reserved;
      index = i;
      break;
    }
  }
  mtx_unlock(&pty_table_lock);

  if (index < 0) {
    EPRINTF("no free pty slots\n");
    return -ENOSPC;
  }

  pty_t *pty = kmallocz(sizeof(pty_t));
  pty->flags = PTYF_LOCKED;

  tty_t *tty = tty_alloc(&pty_ttydev_ops, pty);
  pty->tty = tty;

  knlist_init(&pty->master_knlist, &tty->lock.lo);

  // register the slave device with the chosen minor number
  device_t *slave_dev = alloc_device(tty, NULL, NULL);
  if (register_dev_minor("pty", slave_dev, index) < 0) {
    EPRINTF("failed to register slave device\n");
    mtx_lock(&pty_table_lock);
    pty_table[index] = NULL;
    mtx_unlock(&pty_table_lock);
    slave_dev->data = NULL;
    free_device(slave_dev);
    tty_free(&tty);
    kfree(pty);
    return -ENOMEM;
  }
  pty->slave_dev = slave_dev;

  pty->index = index;
  ASSERT(pty->index > 0 && pty->index < MAX_PTYS);

  // create the slave device node synchronously so it's available immediately
  devfs_mknod(slave_dev);

  mtx_lock(&pty_table_lock);
  pty_table[pty->index] = pty;
  mtx_unlock(&pty_table_lock);

  file->type = FT_PTS;
  file->udata = pty;

  DPRINTF("allocated pty %d\n", pty->index);
  return 0;
}

static int ptmx_f_close(file_t *file) {
  f_lock_assert(file, LA_OWNED);
  pty_t *pty = file->udata;
  if (pty == NULL) {
    return 0;
  }

  tty_t *tty = pty->tty;
  pty->flags |= PTYF_MASTER_CLOSED;

  if (tty_lock(tty)) {
    tty->flags |= TTYF_GONE;
    cond_broadcast(&tty->in_data_cond);
    cond_broadcast(&tty->out_data_cond);
    cond_broadcast(&tty->outready_cond);
    cond_broadcast(&tty->dcd_cond);

    if (tty->pgrp) {
      pgroup_t *pgrp = tty->pgrp;
      pgrp_lock(pgrp);
      pgrp_signal(pgrp, &(siginfo_t){.si_signo = SIGHUP});
      pgrp_unlock(pgrp);
    }
    tty_unlock(tty);
  }

  DPRINTF("closed pty %d master\n", pty->index);
  return 0;
}

static ssize_t ptmx_f_read(file_t *file, kio_t *kio) {
  f_lock_assert(file, LA_OWNED);
  pty_t *pty = file->udata;
  tty_t *tty = pty->tty;

  f_unlock(file);

  if (!tty_lock(tty)) {
    f_lock(file);
    return -ENXIO;
  }

  ssize_t total = 0;
  while (kio_remaining(kio) > 0) {
    size_t avail = ttyoutq_bytes(tty->outq);
    if (avail > 0) {
      size_t before = kio_transfered(kio);
      int res = ttyoutq_read(tty->outq, kio);
      size_t nread = kio_transfered(kio) - before;
      if (res < 0 && res != -EAGAIN) {
        if (total == 0)
          total = res;
        break;
      }
      total += (ssize_t)nread;

      if (!ttyoutq_isfull(tty->outq)) {
        tty_signal_cond(tty, &tty->outready_cond);
      }
      break;
    }

    if (pty->flags & PTYF_MASTER_CLOSED) {
      break;
    }

    if (file->flags & O_NONBLOCK) {
      if (total == 0)
        total = -EAGAIN;
      break;
    }

    int res = tty_wait_cond(tty, &tty->out_data_cond);
    if (res < 0) {
      if (total == 0)
        total = res;
      break;
    }
  }

  tty_unlock(tty);
  f_lock(file);
  return total;
}

static ssize_t ptmx_f_write(file_t *file, kio_t *kio) {
  f_lock_assert(file, LA_OWNED);
  pty_t *pty = file->udata;
  tty_t *tty = pty->tty;

  f_unlock(file);

  if (!tty_lock(tty)) {
    f_lock(file);
    return -ENXIO;
  }

  ssize_t total = 0;
  char ch;
  while (kio_read_ch(&ch, kio) > 0) {
    int res = ttydisc_rint(tty, (uint8_t)ch, 0);
    if (res < 0) {
      if (total == 0)
        total = res;
      break;
    }
    total++;
  }

  if (total > 0) {
    ttydisc_rint_done(tty);
  }

  tty_unlock(tty);
  f_lock(file);
  return total;
}

static int ptmx_f_stat(file_t *file, struct stat *statbuf) {
  f_lock_assert(file, LA_OWNED);
  memset(statbuf, 0, sizeof(*statbuf));
  statbuf->st_mode = S_IFCHR | 0666;
  return 0;
}

static int ptmx_f_ioctl(file_t *file, unsigned int request, void *arg) {
  f_lock_assert(file, LA_OWNED);
  pty_t *pty = file->udata;
  tty_t *tty = pty->tty;

  switch (request) {
    case TIOCGPTN: {
      if (vm_validate_ptr((uintptr_t)arg, /*write=*/true) < 0)
        return -EINVAL;
      *(unsigned int *)arg = (unsigned int)pty->index;
      DPRINTF("TIOCGPTN -> %d\n", pty->index);
      return 0;
    }
    case TIOCSPTLCK: {
      if (vm_validate_ptr((uintptr_t)arg, /*write=*/false) < 0)
        return -EINVAL;
      int lock = *(int *)arg;
      if (lock)
        pty->flags |= PTYF_LOCKED;
      else
        pty->flags &= ~PTYF_LOCKED;
      DPRINTF("TIOCSPTLCK -> %d\n", lock);
      return 0;
    }
    case TIOCSWINSZ:
    case TIOCGWINSZ:
    case TCGETS:
    case TCSETS:
    case TCSETSW:
    case TCSETSF:
    case TIOCGPGRP:
    case TIOCSPGRP:
    case TIOCSCTTY:
    case TIOCNOTTY:
    case TIOCGSID: {
      if (!tty_lock(tty))
        return -ENXIO;
      int res = tty_ioctl(tty, request, arg);
      tty_unlock(tty);
      return res;
    }
    default:
      EPRINTF("unsupported ioctl %#x\n", request);
      return -ENOTTY;
  }
}

static int ptmx_f_kqevent(file_t *file, knote_t *kn) {
  pty_t *pty = file->udata;
  tty_t *tty = pty->tty;
  ASSERT(kn->event.filter == EVFILT_READ);

  size_t nbytes = ttyoutq_bytes(tty->outq);
  if (nbytes > 0) {
    kn->event.data = (intptr_t)nbytes;
    return 1;
  }
  return 0;
}

static void ptmx_f_cleanup(file_t *file) {
  // release the vnode reference from the original device open
  vnode_t *vn = moveptr(file->data);
  if (vn) {
    vn_putref(&vn);
  }

  pty_t *pty = moveptr(file->udata);
  if (pty == NULL) {
    return;
  }

  DPRINTF("cleaning up pty %d\n", pty->index);

  mtx_lock(&pty_table_lock);
  pty_table[pty->index] = NULL;
  mtx_unlock(&pty_table_lock);

  device_t *slave_dev = pty->slave_dev;
  pty->slave_dev = NULL;
  if (slave_dev) {
    devfs_unlink(slave_dev);
    unregister_dev(slave_dev);
    slave_dev->data = NULL;
    free_device(slave_dev);
  }

  tty_free(&pty->tty);
  kfree(pty);
}

static struct file_ops ptmx_file_ops = {
  .f_open = ptmx_f_open,
  .f_close = ptmx_f_close,
  .f_read = ptmx_f_read,
  .f_write = ptmx_f_write,
  .f_stat = ptmx_f_stat,
  .f_ioctl = ptmx_f_ioctl,
  .f_kqevent = ptmx_f_kqevent,
  .f_cleanup = ptmx_f_cleanup,
};

// =========================================================================
// /dev/tty - controlling terminal device
// =========================================================================

static tty_t *ctty_get_tty(file_t *file) {
  return (tty_t *) file->udata;
}

static int ctty_f_open(file_t *file, int flags) {
  f_lock_assert(file, LA_OWNED);
  session_t *sess = curproc->group->session;
  sess_lock(sess);
  tty_t *tty = sess->tty;
  sess_unlock(sess);
  if (tty == NULL)
    return -ENXIO;

  if (!tty_lock(tty))
    return -ENXIO;
  int res = tty_open(tty);
  tty_unlock(tty);
  if (res < 0)
    return res;

  file->udata = tty;
  return 0;
}

static int ctty_f_close(file_t *file) {
  f_lock_assert(file, LA_OWNED);
  tty_t *tty = ctty_get_tty(file);
  if (tty == NULL)
    return 0;
  if (!tty_lock(tty))
    return 0;
  int res = tty_close(tty);
  tty_unlock(tty);
  file->udata = NULL;
  return res;
}

static ssize_t ctty_f_read(file_t *file, kio_t *kio) {
  tty_t *tty = ctty_get_tty(file);
  if (tty == NULL)
    return -ENXIO;
  if (!tty_lock(tty))
    return -ENXIO;
  ssize_t res = ttydisc_read(tty, kio);
  tty_unlock(tty);
  return res;
}

static ssize_t ctty_f_write(file_t *file, kio_t *kio) {
  tty_t *tty = ctty_get_tty(file);
  if (tty == NULL)
    return -ENXIO;
  if (!tty_lock(tty))
    return -ENXIO;
  ssize_t res = ttydisc_write(tty, kio);
  tty_unlock(tty);
  return res;
}

static int ctty_f_ioctl(file_t *file, unsigned int request, void *arg) {
  tty_t *tty = ctty_get_tty(file);
  if (tty == NULL)
    return -ENXIO;
  if (!tty_lock(tty))
    return -ENXIO;
  int res = tty_ioctl(tty, request, arg);
  tty_unlock(tty);
  return res;
}

static void ctty_f_cleanup(file_t *file) {
  file->data = NULL;
  file->udata = NULL;
}

static struct file_ops ctty_file_ops = {
  .f_open = ctty_f_open,
  .f_close = ctty_f_close,
  .f_read = ctty_f_read,
  .f_write = ctty_f_write,
  .f_ioctl = ctty_f_ioctl,
  .f_cleanup = ctty_f_cleanup,
};

// =========================================================================
// Initialization
// =========================================================================

static void pty_module_init() {
  mtx_init(&pty_table_lock, 0, "pty_table_lock");
  memset(pty_table, 0, sizeof(pty_table));

  int major = dev_major_by_name("pty");
  ASSERT(major > 0);

  register_device_ops("pty", &tty_dev_ops);

  // ptmx device at minor 0: /dev/ptmx
  devfs_register_class(major, 0, "ptmx", 0);
  // slave devices at minor 1+: /dev/pts/<minor>
  devfs_register_class(major, -1, "pts/", DEVFS_NUMBERED);

  // pre-create /dev/pts directory so devfs_mknod doesn't need fs_mkdir
  fs_mkdir(cstr_make("/dev/pts"), 0755);

  device_t *ptmx_dev = alloc_device(NULL, NULL, &ptmx_file_ops);
  if (register_dev("pty", ptmx_dev) < 0) {
    panic("pty: failed to register ptmx device");
  }

  // /dev/tty - controlling terminal device
  int ctty_major = dev_major_by_name("ctty");
  ASSERT(ctty_major > 0);
  devfs_register_class(ctty_major, 0, "tty", 0);
  device_t *ctty_dev = alloc_device(NULL, NULL, &ctty_file_ops);
  if (register_dev("ctty", ctty_dev) < 0) {
    panic("pty: failed to register /dev/tty device");
  }

  DPRINTF("initialized (ptmx at major %d)\n", major);
}
MODULE_INIT(pty_module_init);
