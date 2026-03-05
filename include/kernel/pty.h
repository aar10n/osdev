//
// Created by Aaron Gill-Braun on 2026-03-04.
//

#ifndef KERNEL_PTY_H
#define KERNEL_PTY_H

#include <kernel/base.h>
#include <kernel/kevent.h>

struct tty;

#define PTYF_LOCKED         0x01
#define PTYF_MASTER_CLOSED  0x02

#define MAX_PTYS 64

typedef struct pty {
  int index;
  uint32_t flags;
  struct tty *tty;
  struct device *slave_dev;
  struct knlist master_knlist;
} pty_t;

#endif
