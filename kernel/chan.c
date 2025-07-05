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
#define DPRINTF(x, ...)
// #define DPRINTF(x, ...) kprintf("chan: " x, ##__VA_ARGS__)
#define EPRINTF(x, ...) kprintf("chan: %s: " x, __func__, ##__VA_ARGS__)

// private flags
#define CHAN_CLOSED 0x10000  // channel is closed

static inline uint16_t chan_count(chan_t *ch) {
  if (ch->write_idx >= ch->read_idx) {
    return ch->write_idx - ch->read_idx;
  } else {
    return (ch->capacity - ch->read_idx) + ch->write_idx;
  }
}

static inline bool chan_is_closed(chan_t *ch) {
  return (ch->flags & CHAN_CLOSED) != 0;
}

static inline uint16_t get_next_index(chan_t *ch, uint16_t curr_index) {
  return (curr_index + 1) % ch->capacity;
}

static inline void *get_slot_ptr(chan_t *ch, uint16_t index) {
  return (char *)ch->buffer + ((uint64_t)index * ch->objsize);
}

//
// Public API
//

chan_t *chan_alloc(size_t capacity, size_t objsize, uint32_t flags, const char *name) {
  if (capacity == 0 || objsize == 0)
    return NULL;

  ASSERT(capacity <= CHAN_CAPACITY_MAX);
  ASSERT(objsize <= CHAN_OBJSIZE_MAX);
  chan_t *ch = kmalloc(sizeof(chan_t));
  if (!ch)
    return NULL;

  ch->flags = flags;
  ch->name = name;

  ch->buffer = kmalloc(capacity * objsize);
  if (!ch->buffer) {
    kfree(ch);
    return NULL;
  }

  mtx_init(&ch->lock, MTX_SPIN|MTX_RECURSIVE, name);
  cond_init(&ch->send_cond, name);
  cond_init(&ch->recv_cond, name);

  ch->capacity = capacity;
  ch->objsize = objsize;
  ch->read_idx = 0;
  ch->write_idx = 0;

  ch->free_cb = NULL;
  return ch;
}

int chan_set_free_cb(chan_t *ch, chan_free_cb_t fn) {
  ch->free_cb = fn;
  return 0;
}

int chan_free(chan_t *ch) {
  ASSERT(ch != NULL);
  if (!chan_is_closed(ch)) {
    EPRINTF("error: calling `chan_free()` on open channel '%s'\n", ch->name);
    return -EINVAL;
  }

  mtx_spin_lock(&ch->lock);

  if (ch->free_cb) {
    // call the free callback on remaining objects in the buffer
    while (ch->read_idx != ch->write_idx) {
      void *slot = ch->buffer + ((uint64_t)ch->read_idx * ch->objsize);
      ch->free_cb(slot);
      ch->read_idx = get_next_index(ch, ch->read_idx);
    }
  }

  cond_destroy(&ch->send_cond);
  cond_destroy(&ch->recv_cond);
  mtx_spin_unlock(&ch->lock);
  mtx_destroy(&ch->lock);

  // free the buffer
  kfree(ch->buffer);
  kfree(ch);
  return 0;
}

//

int _chan_send(chan_t *ch, void *obj, size_t objsz) {
  ASSERT(ch != NULL && obj != NULL);
  ASSERT(objsz == ch->objsize);
  mtx_spin_lock(&ch->lock);

  if (chan_is_closed(ch)) {
    EPRINTF("error: calling `chan_send()` on closed channel '%s'\n", ch->name);
    if (ch->free_cb) {
      // call the free callback on the object to prevent memory leaks
      ch->free_cb(obj);
    }
    mtx_spin_unlock(&ch->lock);
    return -EPIPE;
  }

  if (chan_count(ch) == ch->capacity) {
    if (ch->flags & CHAN_NOBLOCK) {
      // drop the object at the read head if the channel is full and
      // the channel is in non-blocking mode.
      void *slot = get_slot_ptr(ch, ch->read_idx);
      if (ch->free_cb)
        ch->free_cb(slot);

      memcpy(slot, obj, ch->objsize);
      ch->read_idx = get_next_index(ch, ch->read_idx);
      ch->write_idx = ch->read_idx;
    } else {
      // wait for space to become available
      DPRINTF("channel '%s' is full, waiting for space\n", ch->name);
      while (chan_count(ch) == ch->capacity && !chan_is_closed(ch))
        cond_wait(&ch->recv_cond, &ch->lock);

      if (chan_is_closed(ch)) {
        mtx_spin_unlock(&ch->lock);
        return -EPIPE;
      }

      void *slot = get_slot_ptr(ch, ch->write_idx);
      memcpy(slot, obj, ch->objsize);
      ch->write_idx = get_next_index(ch, ch->write_idx);
    }
  } else {
    // write the data into the buffer normally
    void *slot = get_slot_ptr(ch, ch->write_idx);
    memcpy(slot, obj, ch->objsize);
    ch->write_idx = get_next_index(ch, ch->write_idx);
  }

  DPRINTF("sent object to channel '%s', count=%d, send_cond=%p\n", ch->name, chan_count(ch), &ch->send_cond);
  cond_signal(&ch->send_cond);
  mtx_spin_unlock(&ch->lock);
  return 0;
}

int _chan_recv(chan_t *ch, void *obj, size_t objsz) {
  ASSERT(ch != NULL);
  ASSERT(objsz == ch->objsize);
  mtx_spin_lock(&ch->lock);

  // wait for data to become available
  while (chan_count(ch) == 0 && !chan_is_closed(ch))
    cond_wait(&ch->send_cond, &ch->lock);

  if (chan_count(ch) == 0 && chan_is_closed(ch)) {
    mtx_spin_unlock(&ch->lock);
    return -EPIPE;
  }

  void *slot = get_slot_ptr(ch, ch->read_idx);
  ch->read_idx = get_next_index(ch, ch->read_idx);
  if (obj != NULL) {
    memcpy(obj, slot, ch->objsize);
  } else if (ch->free_cb) {
    // no result pointer provided, equivalent to dropping the object
    ch->free_cb(slot);
  }

  cond_signal(&ch->recv_cond);
  mtx_spin_unlock(&ch->lock);
  return 0;
}

int _chan_recvn(chan_t *ch, size_t n, void *results, size_t objsz) {
  ASSERT(ch != NULL);
  ASSERT(n <= INT32_MAX);
  ASSERT(objsz == ch->objsize);
  if (!results || n == 0)
    return -EINVAL;

  mtx_spin_lock(&ch->lock);

  size_t received = 0;
  while (received < n) {
    while (chan_count(ch) == 0 && !chan_is_closed(ch))
      cond_wait(&ch->send_cond, &ch->lock);

    if (chan_count(ch) == 0 && chan_is_closed(ch)) {
      mtx_spin_unlock(&ch->lock);
      return received > 0 ? (int)received : -EPIPE;
    }

    void *dst = (char *)results + (received * ch->objsize);
    void *src = get_slot_ptr(ch, ch->read_idx);
    memcpy(dst, src, ch->objsize);
    ch->read_idx = get_next_index(ch, ch->read_idx);
    received++;

    cond_signal(&ch->recv_cond);
  }

  mtx_spin_unlock(&ch->lock);
  return received > 0 ? (int)received : -EPIPE;
}

int _chan_recv_noblock(chan_t *ch, void *obj, size_t objsz) {
  ASSERT(ch != NULL);
  mtx_spin_lock(&ch->lock);
  ASSERT(objsz == ch->objsize);

  if (chan_count(ch) == 0) {
    int res;
    if (chan_is_closed(ch)) {
      res = -EPIPE; // channel is closed
    } else {
      res = -EAGAIN; // receive operation would block
    }
    mtx_spin_unlock(&ch->lock);
    return res;
  }

  void *slot = get_slot_ptr(ch, ch->read_idx);
  ch->read_idx = get_next_index(ch, ch->read_idx);
  if (obj != NULL) {
    memcpy(obj, slot, ch->objsize);
  } else if (ch->free_cb) {
    ch->free_cb(slot);
  }

  cond_signal(&ch->recv_cond);
  mtx_spin_unlock(&ch->lock);
  return 0;
}

int _chan_recv_opts(chan_t *ch, void *obj, size_t objsz, int opts) {
  if (opts & CHAN_RX_NOBLOCK) {
    return _chan_recv_noblock(ch, obj, objsz);
  } else {
    return _chan_recv(ch, obj, objsz);
  }
}

int chan_wait(chan_t *ch) {
  ASSERT(ch != NULL);
  mtx_spin_lock(&ch->lock);

  while (chan_count(ch) == 0 && !chan_is_closed(ch))
    cond_wait(&ch->send_cond, &ch->lock);

  if (chan_count(ch) == 0 && chan_is_closed(ch)) {
    mtx_spin_unlock(&ch->lock);
    return -EPIPE;
  }

  mtx_spin_unlock(&ch->lock);
  return 0;
}

int chan_close(chan_t *ch) {
  ASSERT(ch != NULL);
  mtx_spin_lock(&ch->lock);
  if (chan_is_closed(ch)) {
    EPRINTF("error: calling `chan_close()` on already closed channel '%s'\n", ch->name);
    mtx_spin_unlock(&ch->lock);
    return -EALREADY;
  }

  ch->flags |= CHAN_CLOSED;
  cond_broadcast(&ch->send_cond);
  cond_broadcast(&ch->recv_cond);
  mtx_spin_unlock(&ch->lock);
  return 0;
}
