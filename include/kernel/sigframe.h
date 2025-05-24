//
// Created by Aaron Gill-Braun on 2024-12-13.
//

#ifndef KERNEL_SIGFRAME_H
#define KERNEL_SIGFRAME_H

#include <abi/signal.h>

struct sigframe {
  struct siginfo info;    // 0x00
  struct sigaction act;   // 0x30
  struct sigcontext ctx;  // 0x50
};
_Static_assert(sizeof(struct sigframe) == 336, "");

#endif
