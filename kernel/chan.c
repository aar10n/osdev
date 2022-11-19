//
// Created by Aaron Gill-Braun on 2022-08-23.
//

#include <chan.h>
#include <mm.h>

#include <printf.h>
#include <panic.h>
#include <string.h>
#include <atomic.h>

static id_t chan_id = 0;

static inline uint16_t get_next_index(chan_t *chan, uint16_t curr_index) {
  if (curr_index == chan->max_idx) {
    return 0;
  }
  return curr_index + 1;
}

static inline int cleanup_data(chan_t *chan, uint64_t data) {
  // free or drop the old entry
  if (data != 0 && chan->free_cb != NULL) {
    kassert(chan->free_cb != NULL);
    chan->free_cb((void *)(data));
    return 0;
  }
  return 0;
}


// Public API

chan_t *chan_alloc(uint16_t size, uint32_t flags) {
  if (size == 0) {
    return NULL;
  }

  // mask internal flags
  flags &= ~CHAN_CLOSED;

  chan_t *chan = kmalloc(sizeof(chan_t));
  memset(chan, 0, sizeof(chan_t));
  chan->id = atomic_fetch_add(&chan_id, 1);
  chan->flags = flags;
  mutex_init(&chan->lock, MUTEX_REENTRANT);

  chan->max_idx = size - 1;
  chan->read_idx = 0;
  chan->write_idx = 0;
  chan->buffer = kmalloc(size * sizeof(uint64_t));

  mutex_init(&chan->reader, MUTEX_REENTRANT);
  cond_init(&chan->data_read, COND_NOEMPTY);
  mutex_init(&chan->writer, MUTEX_REENTRANT);
  cond_init(&chan->data_written, COND_NOEMPTY);
  return chan;
}

int chan_free(chan_t *chan) {
  if (!(chan->flags & CHAN_CLOSED)) {
    kprintf("error: calling `chan_free()` on open channel[%u]\n", chan->id);
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
  if (chan->flags & CHAN_CLOSED) {
    kprintf("error: calling `chan_send()` on closed channel[%u]\n", chan->id);
    return -1;
  }

  mutex_lock(&chan->writer);
  mutex_lock(&chan->lock);
  cond_clear_signal(&chan->data_written);

  // we can write the data into the buffer normally
  chan->buffer[chan->write_idx] = data;
  if (get_next_index(chan, chan->write_idx) == chan->read_idx) {
    // we're about to wrap around the read pointer, so we will instead
    // push both the write pointer and read pointer forward by one, and
    // free/drop the data that was under the read head previously.
    //
    // before:
    //     write index    v
    //      read index      v
    //          buffer | | | | | |
    // after:
    //     write index      v
    //      read index        v
    //          buffer | | | | | |

    uint16_t read_idx = chan->read_idx;
    uint64_t old_data = chan->buffer[read_idx];
    chan->buffer[read_idx] = 0;

    // bump pointers forward
    chan->read_idx = get_next_index(chan, chan->read_idx);
    chan->write_idx = get_next_index(chan, chan->write_idx);

    // free or drop the old entry
    cleanup_data(chan, old_data);
  } else {
    // advance the write pointer forward
    chan->write_idx = get_next_index(chan, chan->write_idx);
  }

  cond_signal(&chan->data_written);
  mutex_unlock(&chan->lock);
  mutex_unlock(&chan->writer);
  return 0;
}

int chan_sendb(chan_t *chan, uint64_t data) {
  if (chan->flags & CHAN_CLOSED) {
    kprintf("error: calling `chan_sendb()` on closed channel[%u]\n", chan->id);
    return -1;
  }

  // blocking send
  mutex_lock(&chan->writer);

  if (chan_send(chan, data) < 0) {
    mutex_unlock(&chan->writer);
    return -1;
  }

  cond_wait(&chan->data_read);
  mutex_unlock(&chan->writer);
  return 0;
}

int chan_recv(chan_t *chan, uint64_t *result) {
  if (chan->flags & CHAN_CLOSED) {
    return -1;
  }

  mutex_lock(&chan->reader);
  mutex_lock(&chan->lock);
  cond_clear_signal(&chan->data_read);

  if (chan->read_idx == chan->write_idx) {
    mutex_unlock(&chan->lock);
    // wait for new data to be written
    cond_wait(&chan->data_written);
    if (chan->flags & CHAN_CLOSED) {
      mutex_unlock(&chan->reader);
      return -1;
    }
    //
    mutex_lock(&chan->lock);
  }
  kassert(chan->read_idx != chan->write_idx);

  // take data off the buffer
  uint64_t data = chan->buffer[chan->read_idx];
  chan->buffer[chan->read_idx] = 0;
  chan->read_idx = get_next_index(chan, chan->read_idx);

  cond_signal(&chan->data_read);
  mutex_unlock(&chan->lock);

  if (result != NULL) {
    *result = data;
  } else {
    // drop the ignored data
    cleanup_data(chan, data);
  }

  mutex_unlock(&chan->reader);
  return 0;
}

int chan_recv_noblock(chan_t *chan, uint64_t *result) {
  if (chan->flags & CHAN_CLOSED) {
    return -1;
  }

  mutex_lock(&chan->reader);
  mutex_lock(&chan->lock);
  cond_clear_signal(&chan->data_read);

  if (chan->read_idx == chan->write_idx) {
    mutex_unlock(&chan->lock);
    mutex_unlock(&chan->reader);
    return -2; // no data to read (without blocking)
  }
  kassert(chan->read_idx != chan->write_idx);

  // take data off the buffer
  uint64_t data = chan->buffer[chan->read_idx];
  chan->buffer[chan->read_idx] = 0;
  chan->read_idx = get_next_index(chan, chan->read_idx);

  cond_signal(&chan->data_read);
  mutex_unlock(&chan->lock);

  if (result != NULL) {
    *result = data;
  } else {
    // drop the ignored data
    cleanup_data(chan, data);
  }

  mutex_unlock(&chan->reader);
  return 0;
}

int chan_recvn(chan_t *chan, size_t n, uint64_t *results) {
  if (n == 0 || chan->flags & CHAN_CLOSED) {
    return -1;
  }

  mutex_lock(&chan->reader);

  for (size_t i = 0; i < n; i++) {
    uint64_t result;
    if (chan_recv(chan, &result) < 0) {
      mutex_unlock(&chan->reader);
      return -1;
    }

    if (results != NULL) {
      results[i] = result;
    }
  }

  mutex_unlock(&chan->reader);
  return 0;
}

int chan_close(chan_t *chan) {
  if (chan->flags & CHAN_CLOSED) {
    kprintf("error: calling `chan_close()` on closed channel[%u]\n", chan->id);
    return -1;
  }

  mutex_lock(&chan->writer);
  mutex_lock(&chan->lock);

  // set closed flag
  chan->flags |= CHAN_CLOSED;

  // cleanup any remaining data
  while (chan->read_idx != chan->write_idx) {
    uint64_t data = chan->buffer[chan->read_idx];
    chan->buffer[chan->read_idx] = 0;
    chan->read_idx = get_next_index(chan, chan->read_idx);
    // free the data using provided callback
    cleanup_data(chan, data);
  }

  mutex_unlock(&chan->lock);
  mutex_unlock(&chan->writer);
  return 0;
}

// common channel free callbacks

void chan_free_cb_kfree(void *data) {
  kfree(data);
}

