//
// Created by Aaron Gill-Braun on 2020-10-23.
//

#include "ringbuf.h"
#include <string.h>

#ifndef _malloc
#include <mm/heap.h>
#define _malloc(size) kmalloc(size)
#define _free(ptr) kfree(ptr)
#endif


ringbuf_t *create_ringbuf(size_t size) {
  void *buf = _malloc(size);
  memset(buf, 0, size);

  ringbuf_t *ringbuf = _malloc(sizeof(ringbuf_t));
  ringbuf->buffer = buf;
  ringbuf->read_index = 0;
  ringbuf->write_index = 0;
  ringbuf->size = size;
  return ringbuf;
}

void destroy_ringbuf(ringbuf_t *buffer) {
  _free(buffer->buffer);
  _free(buffer);
}

void ringbuf_write64(ringbuf_t *buffer, uint64_t value) {
  uintptr_t addr = (uintptr_t) buffer->buffer + buffer->write_index;
  *((uint64_t *)(addr)) = value;
  if (buffer->write_index + sizeof(uint64_t) >= buffer->size) {
    buffer->write_index = 0;
  } else {
    buffer->write_index += sizeof(uint64_t);
  }
}

void ringbuf_writechar(ringbuf_t *buffer, char value) {
  char *ptr = buffer->buffer + buffer->write_index;
  *ptr = value;
  if (buffer->write_index + sizeof(char) >= buffer->size) {
    buffer->write_index = 0;
  } else {
    buffer->write_index += sizeof(char);
  }
}

uint64_t ringbuf_read64(ringbuf_t *buffer) {
  uintptr_t addr = (uintptr_t) buffer->buffer + buffer->read_index;
  uint64_t value = *((uint64_t *)(addr));
  if (buffer->read_index + sizeof(uint64_t) >= buffer->size) {
    buffer->read_index = 0;
  } else {
    buffer->read_index += sizeof(uint64_t);
  }
  return value;
}

char ringbuf_readchar(ringbuf_t *buffer) {
  uintptr_t addr = (uintptr_t) buffer->buffer + buffer->read_index;
  char value = *((char *)(addr));
  if (buffer->read_index + 1 >= buffer->size) {
    buffer->read_index = 0;
  } else {
    buffer->read_index += sizeof(char);
  }
  return value;
}
