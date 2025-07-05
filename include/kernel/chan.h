//
// Created by Aaron Gill-Braun on 2022-08-23.
//

#ifndef KERNEL_CHAN_H
#define KERNEL_CHAN_H

#include <kernel/base.h>
#include <kernel/mutex.h>
#include <kernel/cond.h>

// -------- Channels --------

// The `free` callback function is intended to be used when transfering objects
// over a channel which need to be freed if the data needs to be dropped and is
// not received.
typedef void (*chan_free_cb_t)(void *data);

#define CHAN_CAPACITY_MAX UINT16_MAX
#define CHAN_OBJSIZE_MAX UINT16_MAX

// flags
#define CHAN_NOBLOCK 0x01  // channel operations do not block

// recv_opts
#define CHAN_RX_NOBLOCK 0x01 // the recv operation does not block

typedef struct chan {
  uint32_t flags;
  const char *name;

  mtx_t lock;
  cond_t send_cond;
  cond_t recv_cond;

  uint16_t capacity;
  uint16_t objsize;
  uint16_t read_idx;
  uint16_t write_idx;
  void *buffer;

  chan_free_cb_t free_cb;
} chan_t;

// Public API

chan_t *chan_alloc(size_t  capacity, size_t objsize, uint32_t flags, const char *name);
int chan_set_free_cb(chan_t *ch, chan_free_cb_t fn);
int chan_free(chan_t *ch);

int _chan_send(chan_t *ch, void *obj, size_t objsz);
#define chan_send(ch, obj) _chan_send(ch, obj, sizeof(*(obj)))
int _chan_recv(chan_t *ch, void *obj, size_t objsz);
#define chan_recv(ch, obj) _chan_recv(ch, obj, sizeof(*(obj)))
int _chan_recvn(chan_t *ch, size_t n, void *results, size_t objsz);
#define chan_recvn(ch, n, results) _chan_recvn(ch, n, results, sizeof(*(results)))
int _chan_recv_noblock(chan_t *ch, void *obj, size_t objsz);
#define chan_recv_noblock(ch, obj) _chan_recv_noblock(ch, obj, sizeof(*(obj)))
int _chan_recv_opts(chan_t *ch, void *obj, size_t objsz, int opts);
#define chan_recv_opts(ch, obj, opts) _chan_recv_opts(ch, obj, sizeof(*(obj)), opts)
int chan_wait(chan_t *ch);
int chan_close(chan_t *ch);


#endif
