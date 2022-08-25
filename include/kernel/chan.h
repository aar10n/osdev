//
// Created by Aaron Gill-Braun on 2022-08-23.
//

#ifndef KERNEL_CHAN_H
#define KERNEL_CHAN_H

#include <base.h>
#include <mutex.h>
#include <queue.h>

// -------- Channels --------

// Inspired by the Go construct of the same name, channels provide a
// simplified way to pass data between producer and consumer threads.
// A channel can have exactly one reader and one writer at any given
// time.
//
// By default, sending data to a channel does not block unless the
// channel's buffer is full. Conversely reading does block as long
// as the buffer is empty. This behaviour can be changed via flags.

// The `free` callback function is intended to be used when transfering objects
// over a channel which need to be freed if the data needs to be dropped and is
// not received.
typedef int (*chan_free_cb_t)(void *data);

// flags
#define CHAN_CLOSED 0x01  // channel has been closed by writer


typedef struct chan {
  id_t id;
  uint32_t flags;
  mutex_t lock;

  uint16_t read_idx;
  uint16_t write_idx;
  uint16_t max_idx;
  uint64_t *buffer;
  chan_free_cb_t free_cb;

  mutex_t reader;
  cond_t data_read;
  mutex_t writer;
  cond_t data_written;
} chan_t;

// Public API

chan_t *chan_alloc(uint16_t size, uint32_t flags);
int chan_free(chan_t *chan);
int chan_set_free_cb(chan_t *chan, chan_free_cb_t fn);

int chan_send(chan_t *chan, uint64_t data);
int chan_recv(chan_t *chan, uint64_t *result);
int chan_recvn(chan_t *chan, size_t n, uint64_t *results);
int chan_close(chan_t *chan);

#endif
