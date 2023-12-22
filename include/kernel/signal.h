//
// Created by Aaron Gill-Braun on 2021-03-23.
//

#ifndef KERNEL_SIGNAL_H
#define KERNEL_SIGNAL_H

#include <kernel/base.h>
#include <kernel/queue.h>

#include <abi/signal.h>

typedef struct sigqueue {
  sigset_t signals; // pending signals
  uint32_t flags;   // signal flags
  SLIST_ENTRY(struct sigqueue) next;
} sigqueue_t;

#endif
