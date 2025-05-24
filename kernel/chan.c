//
// Created by Aaron Gill-Braun on 2022-08-23.
//

#include <kernel/chan.h>
#include <kernel/mm.h>

#include <kernel/printf.h>
#include <kernel/panic.h>
#include <kernel/string.h>
#include <kernel/atomic.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(x, ...) kprintf("chan: " x, ##__VA_ARGS__)
#define EPRINTF(x, ...) kprintf("chan: %s: " x, __func__, ##__VA_ARGS__)


static inline uint16_t get_next_index(chan_t *chan, uint16_t curr_index) {
  return (curr_index + 1) % chan->capacity;
}

static inline int cleanup_data(chan_t *chan, uint64_t data) {
  // free or drop the old entry
  if (data != 0 && chan->free_cb != NULL) {
    ASSERT(chan->free_cb != NULL);
    chan->free_cb((void *)(data));
    return 0;
  }
  return 0;
}

//
// Public API
//

chan_t *chan_alloc(uint16_t capacity, uint32_t flags) {
  ASSERT(capacity > 0);

  chan_t *chan = kmallocz(sizeof(chan_t));
  // chan->name = name;
  chan->name = "";
  chan->flags = flags & ~CHAN_CLOSED;
  chan->capacity = capacity;
  chan->read_idx = 0;
  chan->write_idx = 0;
  chan->buffer = kmalloc(capacity * sizeof(uint64_t));
  mtx_init(&chan->lock, MTX_RECURSIVE, "chan_lock");
  sem_init(&chan->empty, 0, capacity, "chan_empty");
  sem_init(&chan->full, 0, 0, "chan_full");
  return chan;
}

int chan_free(chan_t *chan) {
  if (!(chan->flags & CHAN_CLOSED)) {
    EPRINTF("error: calling `chan_free()` on open channel '%s'\n", chan->name);
    return -1;
  }

  // cleanup remaining data
  while (chan->read_idx != chan->write_idx) {
    uint64_t data = chan->buffer[chan->read_idx];
    chan->buffer[chan->read_idx] = 0;
    chan->read_idx = get_next_index(chan, chan->read_idx);

    // free the data using provided callback
    cleanup_data(chan, data);
  }

  kfree(chan->buffer);
  kfree(chan);
  return 0;
}

int chan_set_free_cb(chan_t *chan, chan_free_cb_t fn) {
  chan->free_cb = fn;
  return 0;
}

//

int chan_send(chan_t *chan, uint64_t data) {
  todo();
  // if (chan->flags & CHAN_CLOSED) {
  //   EPRINTF("error: calling `chan_send()` on closed channel[%u]\n", chan->id);
  //   return -1;
  // }
  //
  // sem_down(&chan->empty);
  // mtx_lock(&chan->lock);
  //
  // // we can write the data into the buffer normally
  // chan->buffer[chan->write_idx] = data;
  // if (get_next_index(chan, chan->write_idx) == chan->read_idx) {
  //   // we're about to wrap around the read pointer, so we will instead
  //   // push both the write pointer and read pointer forward by one, and
  //   // free/drop the data that was under the read head previously.
  //   //
  //   // before:
  //   //     write index    v
  //   //      read index      v
  //   //          buffer | | | | | |
  //   // after:
  //   //     write index      v
  //   //      read index        v
  //   //          buffer | | | | | |
  //
  //   uint16_t read_idx = chan->read_idx;
  //   uint64_t old_data = chan->buffer[read_idx];
  //   chan->buffer[read_idx] = 0;
  //
  //   // bump pointers forward
  //   chan->read_idx = get_next_index(chan, chan->read_idx);
  //   chan->write_idx = get_next_index(chan, chan->write_idx);
  //
  //   // free or drop the old entry
  //   DPRINTF("chan: dropping data on channel[%u]\n", chan->id);
  //   cleanup_data(chan, old_data);
  // } else {
  //   // advance the write pointer forward
  //   chan->write_idx = get_next_index(chan, chan->write_idx);
  // }
  //
  // mtx_unlock(&chan->lock);
  // sem_up(&chan->full);
  // return 0;
}

int chan_recv(chan_t *chan, uint64_t *result) {
  todo();
  // if (chan->flags & CHAN_CLOSED) {
  //   EPRINTF("error: calling `chan_recv()` on closed channel '%s'\n", chan->name);
  //   return -1;
  // }
  //
  // sem_down(&chan->full);
  // mtx_lock(&chan->lock);
  //
  //
  // if (chan->read_idx == chan->write_idx) {
  //   mtx_unlock(&chan->lock);
  //   // wait for new data to be written
  //   // cond_wait(&chan->data_written);
  //   if (chan->flags & CHAN_CLOSED) {
  //     EPRINTF("error: closed while waiting in `chan_recv()` on channel '%s'\n", chan->name);
  //     mtx_unlock(&chan->reader);
  //     return -1;
  //   }
  //   //
  //   mtx_lock(&chan->lock);
  // }
  // kassert(chan->read_idx != chan->write_idx);
  //
  // // take data off the buffer
  // uint64_t data = chan->buffer[chan->read_idx];
  // chan->buffer[chan->read_idx] = 0;
  // chan->read_idx = get_next_index(chan, chan->read_idx);
  //
  // // cond_signal(&chan->data_read);
  // mtx_unlock(&chan->lock);
  //
  // if (result != NULL) {
  //   *result = data;
  // } else {
  //   // drop the ignored data
  //   cleanup_data(chan, data);
  // }
  //
  // mtx_unlock(&chan->reader);
  // return 0;
}

int chan_recv_noblock(chan_t *chan, uint64_t *result) {
  todo();
  // if (chan->flags & CHAN_CLOSED) {
  //   kprintf("error: calling `chan_recv_noblock()` on closed channel[%u]\n", chan->id);
  //   return -1;
  // }
  //
  // mtx_lock(&chan->reader);
  // mtx_lock(&chan->lock);
  // // cond_clear_signal(&chan->data_read);
  //
  // if (chan->read_idx == chan->write_idx) {
  //   mtx_unlock(&chan->lock);
  //   mtx_unlock(&chan->reader);
  //   return -2; // no data to read (without blocking)
  // }
  // kassert(chan->read_idx != chan->write_idx);
  //
  // // take data off the buffer
  // uint64_t data = chan->buffer[chan->read_idx];
  // chan->buffer[chan->read_idx] = 0;
  // chan->read_idx = get_next_index(chan, chan->read_idx);
  //
  // // cond_signal(&chan->data_read);
  // mtx_unlock(&chan->lock);
  //
  // if (result != NULL) {
  //   *result = data;
  // } else {
  //   // drop the ignored data
  //   cleanup_data(chan, data);
  // }
  //
  // mtx_unlock(&chan->reader);
  // return 0;
}

int chan_recvn(chan_t *chan, size_t n, uint64_t *results) {
  todo();
  // if (n == 0 || chan->flags & CHAN_CLOSED) {
  //   kprintf("error: calling `chan_recvn()` on closed channel[%u]\n", chan->id);
  //   return -1;
  // }
  //
  // mtx_lock(&chan->reader);
  //
  // for (size_t i = 0; i < n; i++) {
  //   uint64_t result;
  //   if (chan_recv(chan, &result) < 0) {
  //     mtx_unlock(&chan->reader);
  //     return -1;
  //   }
  //
  //   if (results != NULL) {
  //     results[i] = result;
  //   }
  // }
  //
  // mtx_unlock(&chan->reader);
  // return 0;
}

int chan_wait(chan_t *chan) {
  todo();
  // if (chan->flags & CHAN_CLOSED) {
  //   kprintf("error: calling `chan_wait()` on closed channel[%u]\n", chan->id);
  //   return -1;
  // }
  //
  // mtx_lock(&chan->reader);
  // mtx_lock(&chan->lock);
  // // cond_clear_signal(&chan->data_read);
  //
  // if (chan->read_idx == chan->write_idx) {
  //   // wait until there is data to read
  //   mtx_unlock(&chan->lock);
  //   // cond_wait(&chan->data_written);
  //   if (chan->flags & CHAN_CLOSED) {
  //     kprintf("error: closed while waiting in `chan_wait()` on channel[%u]\n", chan->id);
  //     mtx_unlock(&chan->reader);
  //     return -1;
  //   }
  //   return 0;
  // }
  //
  // mtx_unlock(&chan->lock);
  // mtx_unlock(&chan->reader);
  // return 0;
}

int chan_close(chan_t *chan) {
  todo();
  // if (chan->flags & CHAN_CLOSED) {
  //   kprintf("error: calling `chan_close()` on closed channel[%u]\n", chan->id);
  //   return -1;
  // }
  //
  // mtx_lock(&chan->writer);
  // mtx_lock(&chan->lock);
  //
  // // set closed flag
  // chan->flags |= CHAN_CLOSED;
  //
  // // cleanup any remaining data
  // while (chan->read_idx != chan->write_idx) {
  //   uint64_t data = chan->buffer[chan->read_idx];
  //   chan->buffer[chan->read_idx] = 0;
  //   chan->read_idx = get_next_index(chan, chan->read_idx);
  //   // free the data using provided callback
  //   cleanup_data(chan, data);
  // }
  //
  // mtx_unlock(&chan->lock);
  // mtx_unlock(&chan->writer);
  // return 0;
}

// common channel free callbacks

void chan_free_cb_kfree(void *data) {
  kfree(data);
}

