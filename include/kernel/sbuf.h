//
// Created by Aaron Gill-Braun on 2023-04-20.
//

#ifndef INCLUDE_KERNEL_SBUF_H
#define INCLUDE_KERNEL_SBUF_H

#include <base.h>
#include <string.h>

/// StaticBuffer is a simple non-owning wrapper around a byte buffer and
/// provides safe interfaces for reading and writing data.
typedef struct sbuf {
  uint8_t *data; //                data + size v
  size_t size;   // ⌧⌧⌧⌧⌧⌧⌧⌧⌧⌧⌧⌧⌧⌧⌧⌧⌧⌧⌧⌧⌧⌧⌧⌧⌧⌧⌧
  uint8_t *ptr;  // ^ data      ^ ptr
} sbuf_t;
#define sbuf_init(buffer, length) ({ \
  _Static_assert(_Generic(buffer, uint8_t*: 1, int8_t*: 1, char*: 1, default: 0) == 1, \
                  "buffer must be a pointer to a byte buffer"); \
  ((sbuf_t){ .data = (uint8_t*)(buffer), .size = (length), .ptr = (uint8_t*)(buffer) }); \
})

// MARK: - Getters

static inline size_t sbuf_cap(sbuf_t *sbuf) { return sbuf ? sbuf->size : 0; }
static inline size_t sbuf_len(sbuf_t *sbuf) { return sbuf ? sbuf->ptr - sbuf->data : 0; }
static inline size_t sbuf_rem(sbuf_t *sbuf) { return sbuf ? sbuf->size - sbuf_len(sbuf) : 0; }

static inline uint8_t *sbuf_access(sbuf_t *sbuf, size_t index) {
  if (sbuf == NULL || index >= sbuf_len(sbuf))
    return NULL;
  return sbuf->data + index;
}

static inline uint8_t sbuf_peek(sbuf_t *sbuf) {
  if (sbuf && sbuf_len(sbuf) > 0) {
    return *sbuf->ptr;
  }
  return 0;
}

// MARK: - Methods

static inline void sbuf_reset(sbuf_t *sbuf) {
  sbuf && (sbuf->ptr = sbuf->data);
}

static inline size_t sbuf_seek(sbuf_t *sbuf, ssize_t offset) {
  if (sbuf == NULL)
    return 0;

  if (offset < 0) {
    offset = max(offset, -sbuf_len(sbuf));
  } else {
    offset = min(offset, sbuf_rem(sbuf));
  }
  sbuf->ptr = sbuf->data + offset;
  return abs(offset);
}

static inline uint8_t sbuf_pop(sbuf_t *sbuf) {
  if (sbuf == NULL || sbuf_len(sbuf) == 0)
    return 0;
  uint8_t b = *sbuf->ptr;
  *sbuf->ptr = 0;
  sbuf->ptr--;
  return b;
}

static inline void sbuf_reverse(sbuf_t *sbuf) {
  if (sbuf == NULL || sbuf_len(sbuf) == 0)
    return;

  uint8_t *start = sbuf->data;
  uint8_t *end = sbuf->ptr - 1;
  while (start < end) {
    uint8_t tmp = *start;
    *start++ = *end;
    *end-- = tmp;
  }
}

static inline size_t sbuf_read(sbuf_t *sbuf, void *data, size_t size) {
  if (sbuf == NULL)
    return 0;

  size = min(size, sbuf_len(sbuf));
  memcpy(data, sbuf->data, size);
  sbuf->data += size;
  return size;
}

static inline size_t sbuf_write(sbuf_t *sbuf, const void *data, size_t size) {
  if (sbuf == NULL)
    return 0;

  size = min(size, sbuf_rem(sbuf));
  memcpy(sbuf->ptr, data, size);
  sbuf->ptr += size;
  return size;
}

static inline size_t sbuf_write_reverse(sbuf_t *sbuf, const void *data, size_t size) {
  if (sbuf == NULL)
    return 0;

  size = min(size, sbuf_rem(sbuf));
  for (size_t i = 0; i < size; i++) {
    size_t ir = size - i - 1;
    *sbuf->ptr = ((uint8_t*)data)[ir];
    sbuf->ptr++;
  }
  return size;
}

static inline size_t sbuf_write_char(sbuf_t *sbuf, char ch) {
  return sbuf_write(sbuf, &ch, 1);
}

static inline size_t sbuf_write_str(sbuf_t *sbuf, const char *str) {
  return sbuf_write(sbuf, str, strlen(str));
}

#ifdef __FS_TYPES__

#include <path.h>

static inline path_t sbuf_to_path(sbuf_t *sbuf) {
  return strn2path((char *) sbuf->data, sbuf_len(sbuf));
}

#endif // __FS_TYPES__

#endif
