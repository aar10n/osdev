//
// Created by Aaron Gill-Braun on 2020-10-23.
//

#ifndef LIB_RINGBUF_H
#define LIB_RINGBUF_H

#include <base.h>

typedef struct {
  void *buffer;
  size_t size;
  size_t read_index;
  size_t write_index;
} ringbuf_t;

ringbuf_t *create_ringbuf(size_t size);
void destroy_ringbuf();
void ringbuf_write64(ringbuf_t *buffer, uint64_t value);
void ringbuf_writechar(ringbuf_t *buffer, char value);
uint64_t ringbuf_read64(ringbuf_t *buffer);
char ringbuf_readchar(ringbuf_t *buffer);

#endif
