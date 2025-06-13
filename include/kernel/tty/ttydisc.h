//
// Created by Aaron Gill-Braun on 2025-05-30.
//

#ifndef KERNEL_TTY_TTYDISC_H
#define KERNEL_TTY_TTYDISC_H

#include <kernel/base.h>
#include <kernel/kio.h>

typedef struct tty tty_t;
struct termios;

#define TTY_IN_BREAK    0x01  // break received
#define TTY_IN_PARITY   0x02  // parity error received
#define TTY_IN_FRAMING  0x04  // overrun error received

void ttydisc_open(tty_t *tty);
void ttydisc_close(tty_t *tty);
size_t ttydisc_bytesavail(tty_t *tty);
int ttydisc_rint(tty_t *tty, uint8_t ch, int flags);
void ttydisc_rint_done(tty_t *tty);
ssize_t ttydisc_read(tty_t *tty, kio_t *kio);
int	ttydisc_write_ch(tty_t *tty, char ch);
ssize_t	ttydisc_write(tty_t *tty, kio_t *kio);
int ttydisc_echo(tty_t *tty, uint8_t ch, bool quote);
void ttydisc_fill_cc_default(struct termios *t);

#endif
