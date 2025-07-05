//
// Created by Aaron Gill-Braun on 2025-05-30.
//

#include <kernel/tty/ttydisc.h>
#include <kernel/signal.h>
#include <kernel/tty.h>

#include <kernel/printf.h>
#include <kernel/panic.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(fmt, ...) kprintf("tty_disc: " fmt, ##__VA_ARGS__)
#define EPRINTF(fmt, ...) kprintf("tty_disc: %s: " fmt, __func__, ##__VA_ARGS__)

#define TTY_WRITE_STACK 256

// control characters that should be echoed
#define CTL_ECHO(c, q)	(!(q) && ((c) == '\t' || (c) == '\n' || (c) == '\r' || (c) == 0x8))
// control characters that should be escaped with ^X notation
#define CTL_PRINT(c, q)	((c) == 0x7f || ((c) < 0x20 && ((q) || ((c) != '\t' && (c) != '\n'))))

static void ttydisc_get_breaks(tty_t *tty, char breaks[4]) {
  struct termios *t = &tty->termios;
  size_t n = 0;
  breaks[n++] = '\n'; // end of line
  if (t->c_cc[VEOL] != 0)
    breaks[n++] = (char) t->c_cc[VEOL]; // end of line character
  if (t->c_cc[VEOF] != 0)
    breaks[n++] = (char) t->c_cc[VEOF]; // end of file character

  breaks[n] = '\0'; // null-terminate the string
}

static int ttydisc_write_oproc(tty_t *tty, char ch) {
  struct termios *t = &tty->termios;
  int res;
  switch (ch) {
    // todo: handle tab expansion
    case '\n': {
      // newline conversion
      if (t->c_oflag & ONLCR) {
        // convert NL to CRLF
        res = ttyoutq_write_ch(tty->outq, '\r');
        if (res == 0)
          res = ttyoutq_write_ch(tty->outq, '\n');
      } else {
        res = ttyoutq_write_ch(tty->outq, '\n');
      }
      if (res < 0)
        break;

      if (t->c_oflag & (ONLCR|ONLRET)) {
        // update column position
        tty->column = 0;
      }
      break;
    }
    case '\r': {
      // carriage return conversion
      if (t->c_oflag & OCRNL) {
        // convert CR to NL
        ch = '\n';
      }

      if (t->c_oflag & ONOCR && tty->column == 0) {
        return 0; // ignore CR if ONOCR is set
      }

      res = ttyoutq_write_ch(tty->outq, ch);
      break;
    }
    default: {
      // write the character as is
      res = ttyoutq_write_ch(tty->outq, ch);
      break;
    }
  }

  if (res < 0) {
    EPRINTF("failed to write character {:c} to output queue: {:err}\n", ch, res);
  }
  return res;
}

//

void ttydisc_open(tty_t *tty) {
  // todo();
}

void ttydisc_close(tty_t *tty) {
  todo();
}

size_t ttydisc_bytesavail(tty_t *tty) {
  if (tty->termios.c_lflag & ICANON) {
    // canonical mode: return the number of bytes in the current line
    return ttyinq_canonbytes(tty->inq);
  } else {
    // raw mode: return the number of bytes available in the input queue
    return tty->inq->write_pos - tty->inq->read_pos;
  }
}

int ttydisc_rint(tty_t *tty, uint8_t ch, int flags) {
  tty_assert_owned(tty);
  struct termios *t = &tty->termios;
  uint8_t outbuf[3] = {0xff, 0x00};
  size_t outlen = 0;
  bool quote = false;

  if (flags) {
    if (flags & TTY_IN_BREAK) {
      if (t->c_iflag & IGNBRK) {
        return 0; // ignore the break condition
      } else if (t->c_iflag & BRKINT) {
        // signal a break
        ttyinq_write_ch(tty->inq, '\n', /*quote=*/false); // signal end of line
        return 0; // no error
      }
      return -1; // break not handled
    }
  }

  // handle ISTRIP
  if (t->c_iflag & ISTRIP) {
    ch &= 0x80; // strip high bit
  }

  // todo: handle IEXTEN (and VLNEXT, VDISCARD)

  // signal processing
  if (t->c_lflag & ISIG) {
    // todo: support VSTATUS if ICANON|IEXTEN

    int signal = 0;
    if (ch == t->c_cc[VINTR]) {
      signal = SIGINT;
    } else if (ch == t->c_cc[VQUIT]) {
      signal = SIGQUIT;
    } else if (ch == t->c_cc[VSUSP]) {
      signal = SIGTSTP;
    }

    if (signal != 0) {
      DPRINTF("sending signal %d\n", signal);
      tty_signal_pgrp(tty, signal);
    }
  }

  // handle start/stop characters
  if (t->c_iflag & IXON) {
    if (ch == t->c_cc[VSTART]) {
      tty->flags &= ~TTYF_STOPPED; // clear stopped flag
      return 0;
    } else if (ch == t->c_cc[VSTOP]) {
      tty->flags |= TTYF_STOPPED;  // set stopped flag
      return 0;
    }
  }

  // handle CR and NR conversion
  if (ch == '\r') {
    if (t->c_iflag & IGNCR) {
      return 0; // ignore carriage return
    } else if (t->c_iflag & ICRNL) {
      ch = '\n'; // convert CR to NL
    }
  } else if (ch == '\n') {
    if (t->c_iflag & INLCR) {
      ch = '\r'; // convert NL to CR
    }
  }

  // handle canonical line editing
  if (t->c_lflag & ICANON) {
    if (ch == t->c_cc[VERASE]) {
      // handle erase character
      ttyinq_del_ch(tty->inq);
      return 0;
    } else if (ch == t->c_cc[VKILL]) {
      // handle line kill
      ttyinq_kill_line(tty->inq);
      return 0;
    }
    // todo: handle IEXTEN (and VWERASE, VREPRINT)
  }

LABEL(processed);
  if (t->c_iflag & PARMRK && ch == 0xff) {
    // print 0xff 0xff
    outbuf[1] = 0xff;
    outlen = 2;
    quote = true; // mark as quoted
  } else {
    // just store the character
    outbuf[0] = ch;
    outlen = 1;
  }

  goto output;

LABEL(parmrk);
  if (t->c_iflag & PARMRK) {
    // prepend 0xff 0x00 to the character
    outbuf[2] = ch;
    outlen = 3;
    quote = true;
  } else {
    // just store the character
    outbuf[0] = ch;
    outlen = 1;
  }

LABEL(output);
  // store the character
  kio_t kio = kio_new_readable(outbuf, outlen);
  ttyinq_write(tty->inq, &kio, quote);
  // todo: handle IMAXBEL (send BEL if input buffer is full)

  // canonicalize the input buffer:
  //   in raw mode, after every character
  //   in canonical mode, when a line is complete
  if (!(t->c_lflag & ICANON) ||
     (!quote && (ch == '\n' || ch == t->c_cc[VEOL] || ch == t->c_cc[VEOF]))) {
    DPRINTF("canonicalizing input queue\n");
    ttyinq_canonizalize(tty->inq);
  }

  ttydisc_echo(tty, ch, quote);
  return 0;
}

void ttydisc_rint_done(tty_t *tty) {
  tty_assert_owned(tty);
  struct termios *t = &tty->termios;

  // wake up any readers
  cond_broadcast(&tty->in_wait);
  // wake up driver to handle echo output
  if ((t->c_cflag & CLOCAL) || (tty->flags & TTYF_DCDRDY)) {
    // wakeup ttydev for writing immediately if CLOCAL is set
    // (ignore control signals) or if DCD is ready
    tty->dev_ops->tty_outwakeup(tty);
  }
}

ssize_t ttydisc_read_canonical(tty_t *tty, kio_t *kio) {
  struct termios *t = &tty->termios;
  char cbreak[4] = {0};
  ttydisc_get_breaks(tty, cbreak);

  ssize_t nread;
  size_t ndrop;
  char lastc;
  for (;;) {
    size_t len = ttyinq_find_ch(tty->inq, cbreak, &lastc);
    if (len == 0) {
      // no line available, we need to wait for input
      cond_wait(&tty->in_wait, &tty->lock);
      continue;
    }

    ndrop = 0;
    if ((uint8_t)lastc == t->c_cc[VEOF]) {
      len--;   // remove the EOF character from the length
      ndrop++; // drop the EOF character from the buffer
    }

    nread = 0;
    if (len > 0) {
      nread = ttyinq_read(tty->inq, kio, len);
      if (nread < 0) {
        EPRINTF("ttyinq_read failed: {:err}\n", nread);
        break;
      }
    }
    if (ndrop > 0) {
      ttyinq_drop(tty->inq, ndrop);
    }
    break;
  }
  return nread;
}

ssize_t ttydisc_read_raw(tty_t *tty, kio_t *kio) {
  // if (tty_input_available(tp)) {
  //   error = tty_copy_raw_input(tp, uio);
  //   if (error || uio->uio_resid == 0)
  //     break;
  // } else if (tp->t_flags & TF_NONBLOCK) {
  //   error = EAGAIN;
  //   break;
  // } else {
  //   error = tty_wait_for_input(tp);
  //   if (error)
  //     break;
  // }
  todo();
}

ssize_t ttydisc_read(tty_t *tty, kio_t *kio) {
  tty_assert_owned(tty);
  if (tty->termios.c_lflag & ICANON)
    return ttydisc_read_canonical(tty, kio);
  else if (tty->termios.c_cc[VTIME] == 0)
    return ttydisc_read_raw(tty, kio);
  else if (tty->termios.c_cc[VMIN] == 0)
    todo("termios.c_cc[VTIME] > 0 is not supported");
  else
    todo("termios.c_cc[VMIN] > 0 && termios.c_cc[VTIME] > 0 is not supported");
}

int	ttydisc_write_ch(tty_t *tty, char ch) {
  tty_assert_owned(tty);
  struct termios *t = &tty->termios;

  int res;
  if (t->c_oflag & OPOST) {
    // post-process character before output
    res = ttydisc_write_oproc(tty, ch);
  } else {
    // write the character as is
    res = ttyoutq_write_ch(tty->outq, ch);
  }

  if (res < 0) {
    EPRINTF("failed to write character {:#c} to output queue: {:err}\n", ch, res);
    return res;
  }

  if ((t->c_cflag & CLOCAL) || (tty->flags & TTYF_DCDRDY)) {
    // wakeup ttydev for writing immediately if CLOCAL is set
    // (ignore control signals) or if DCD is ready
    tty->dev_ops->tty_outwakeup(tty);
  }
  return res;
}


ssize_t	ttydisc_write(tty_t *tty, kio_t *kio) {
  tty_assert_owned(tty);
  struct termios *t = &tty->termios;

  int res;
  if (t->c_oflag & OPOST) {
    char ch;
    while (kio_read_ch(&ch, kio) > 0) {
      res = ttydisc_write_oproc(tty, ch);
      if (res < 0)
        break;
    }
  } else {
    res = ttyoutq_write(tty->outq, kio);
  }

  if (res < 0) {
    EPRINTF("failed to write to output queue: {:err}\n", res);
    return res;
  }

  if ((t->c_cflag & CLOCAL) || (tty->flags & TTYF_DCDRDY)) {
    // wakeup ttydev for writing immediately if CLOCAL is set
    // (ignore control signals) or if DCD is ready
    tty->dev_ops->tty_outwakeup(tty);
  }
  return (ssize_t) kio_transfered(kio);
}

int ttydisc_echo(tty_t *tty, uint8_t ch, bool quote) {
  struct termios *t = &tty->termios;
  // only echo if ECHO is enabled or ECHONL is enabled and the
  // character is an unquoted newline
  if (!(t->c_lflag & ECHO || ((t->c_lflag & ECHONL) && ch == '\n' && !quote))) {
    return 0;
  }

  if (t->c_lflag & FLUSHO) {
    return 0;
  }

  int res;
  if (t->c_oflag & OPOST && CTL_ECHO(ch, quote)) {
    // postprocess char before output if OPOST is on
    // and the character is an unquoted BS/TB/NL/CR
    res = ttydisc_write_oproc(tty, (char) ch);
  } else if (t->c_lflag & ECHOCTL && CTL_PRINT(ch, quote)) {
    // use ^X notation if ECHOCTL is on and the
    // character is a quoted control character
    char outbuf[4] = "^X\b\b"; // print backspaces

    if (ch != 0x7f) {
      outbuf[1] = (char)(ch + (uint8_t)('A' - 1));
    }

    kio_t kio;
    if (!quote && ch == t->c_cc[VEOL]) {
      kio = kio_new_readable(outbuf, 4);
    } else {
      kio = kio_new_readable(outbuf, 2);
    }
    res = ttyoutq_write(tty->outq, &kio);
  } else {
    // print the character as is
    res = ttyoutq_write_ch(tty->outq, (char) ch);
  }

  if (res < 0) {
    EPRINTF("failed to echo character {:c}: {:err}\n", ch, res);
    return res;
  }

  return 0;
}

void ttydisc_fill_cc_default(struct termios *t) {
  // set default control characters
  t->c_cc[VINTR] = 0x03;    // '^C'
  t->c_cc[VQUIT] = 0x1C;    // '^\'
  t->c_cc[VERASE] = 0x7F;   // '^?'
  t->c_cc[VKILL] = 0x15;    // '^U'
  t->c_cc[VEOF] = 0x04;     // '^D'
  t->c_cc[VTIME] = 0;       // no timeout
  t->c_cc[VMIN] = 1;        // at least one character
  t->c_cc[VSTART] = 0x11;   // '^Q'
  t->c_cc[VSTOP] = 0x13;    // '^S'
  t->c_cc[VSUSP] = 0x1A;    // '^Z'
  t->c_cc[VEOL] = 0x00;     // unset
  t->c_cc[VREPRINT] = 0x12; // '^R'
  t->c_cc[VWERASE] = 0x17;  // '^W'
  t->c_cc[VLNEXT] = 0x16;   // '^V'
  t->c_cc[VEOL2] = 0x00;    // unset

  // zero remaining control characters
  for (size_t i = VEOL2 + 1; i < NCCS; i++) {
    t->c_cc[i] = 0;
  }
}
