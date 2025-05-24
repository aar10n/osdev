//
// Created by Aaron Gill-Braun on 2022-08-23.
//

#ifndef KERNEL_CHAN_H
#define KERNEL_CHAN_H

#include <kernel/base.h>
#include <kernel/mutex.h>
#include <kernel/sem.h>

// -------- Channels --------

// The `free` callback function is intended to be used when transfering objects
// over a channel which need to be freed if the data needs to be dropped and is
// not received.
typedef void (*chan_free_cb_t)(void *data);


#define chan_u64(p) ((uint64_t)(p))
#define chan_voidp(p) ((uint64_t *)((void *)(p)))


// flags
#define CHAN_CLOSED 0x01  // channel has been closed by writer


typedef struct chan {
  const char *name;
  uint32_t flags;
  uint16_t capacity;
  uint16_t read_idx;
  uint16_t write_idx;
  uint64_t *buffer;

  mtx_t lock;
  sem_t empty;
  sem_t full;
  chan_free_cb_t free_cb;
} chan_t;

// Public API

chan_t *chan_alloc(uint16_t capacity, uint32_t flags);
int chan_free(chan_t *chan);
int chan_set_free_cb(chan_t *chan, chan_free_cb_t fn);

int chan_send(chan_t *chan, uint64_t data);
int chan_recv(chan_t *chan, uint64_t *result);
int chan_recv_noblock(chan_t *chan, uint64_t *result);
int chan_wait(chan_t *chan);
int chan_close(chan_t *chan);

void chan_free_cb_kfree(void *data);

#endif
