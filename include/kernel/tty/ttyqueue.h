//
// Created by Aaron Gill-Braun on 2025-05-30.
//

#ifndef KERNEL_TTY_TTYQUEUE_H
#define KERNEL_TTY_TTYQUEUE_H

#include <kernel/base.h>
#include <kernel/queue.h>
#include <kernel/mutex.h>
#include <kernel/kio.h>


struct ttyinq {
  uintptr_t data_buf;     // pointer to the queue data buffer
  size_t data_size;       // size of the queue data buffer
  uint32_t read_pos;      // read position in the queue
  uint32_t write_pos;     // write position in the queue
  uint32_t next_line;     // position of start of next line (== read_pos if no next line)
  uint32_t *quote_buf;    // bitmap of quoted characters
};

struct ttyinq *ttyinq_alloc();
void ttyinq_free(struct ttyinq **inqp);
int ttyinq_setsize(struct ttyinq *inq, size_t size);
void ttyinq_flush(struct ttyinq *inq);
void ttyinq_canonizalize(struct ttyinq *inq);
size_t ttyinq_find_ch(struct ttyinq *inq, const char *chars, char *lastc);
int ttyinq_write_ch(struct ttyinq *inq, char ch, bool quote);
ssize_t ttyinq_write(struct ttyinq *inq, kio_t *kio, bool quote);
ssize_t ttyinq_read(struct ttyinq *inq, kio_t *kio, size_t n);
size_t ttyinq_drop(struct ttyinq *inq, size_t n);
int ttyinq_del_ch(struct ttyinq *inq);

static inline size_t ttyinq_canonbytes(struct ttyinq *inq) {
  kassert(inq->read_pos <= inq->next_line);
  return inq->next_line - inq->read_pos;
}

static inline size_t ttyinq_linebytes(struct ttyinq *inq) {
  kassert(inq->read_pos <= inq->write_pos);
  return inq->write_pos - inq->read_pos;
}

static inline char tty_inq_peek_ch(struct ttyinq *inq) {
  kassert(inq->data_size > 0);
  if (inq->read_pos == inq->write_pos) {
    // queue is empty
    return -1;
  }
  // read the character from the buffer without removing it
  uintptr_t buf_addr = inq->data_buf + inq->read_pos;
  return *(char *)buf_addr;
}


struct ttyoutq {
  uintptr_t data_buf;   // pointer to the queue data buffer
  size_t data_size;     // size of the queue data buffer
  uint32_t read_pos;    // read position in the queue
  uint32_t write_pos;   // write position in the queue
};

struct ttyoutq *ttyoutq_alloc();
void ttyoutq_free(struct ttyoutq **outqp);
int ttyoutq_setsize(struct ttyoutq *outq, size_t size);
void ttyoutq_flush(struct ttyoutq *outq);
int ttyoutq_peek_ch(struct ttyoutq *outq);
int ttyoutq_get_ch(struct ttyoutq *outq);
int ttyoutq_read(struct ttyoutq *outq, kio_t *kio);
int ttyoutq_write_ch(struct ttyoutq *outq, char ch);
int ttyoutq_write(struct ttyoutq *outq, kio_t *kio);

static inline size_t ttyoutq_bytes(struct ttyoutq *outq) {
  kassert(outq->read_pos <= outq->write_pos);
  return outq->write_pos - outq->read_pos;
}

static inline bool ttyoutq_isfull(struct ttyoutq *outq) {
  return (outq->write_pos + 1) % outq->data_size == outq->read_pos;
}

#endif
