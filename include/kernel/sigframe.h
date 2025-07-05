//
// Created by Aaron Gill-Braun on 2024-12-13.
//

#ifndef KERNEL_SIGFRAME_H
#define KERNEL_SIGFRAME_H

#include <abi/signal.h>

struct sigframe {
  struct siginfo info;    // 0x00
  struct sigaction act;   // 0x80
  struct sigcontext ctx;  // 0xA0
};
_Static_assert(sizeof(struct sigframe) == 416, "");

#endif
