//
// Created by Aaron Gill-Braun on 2023-02-17.
//

#ifndef KERNEL_KIO_H
#define KERNEL_KIO_H
#define __KIO__

#include <kernel/base.h>
#include <abi/iov.h>
#include <kernel/panic.h>

typedef enum kio_dir {
  KIO_IN,
  KIO_OUT,
} kio_dir_t;

/// Kernel I/O transfer structure.
///
/// The kio structure is used to represent a data transfer. It does not
/// own the underlying buffers.
typedef struct kio {
  kio_dir_t dir;     // transfer direction
  size_t size;       // total size of the transfer
  union {
    struct {
      void *base;    // buffer base address
      size_t len;    // buffer length
      size_t off;    // current buffer offset
    } buf;
  };
} kio_t;

static inline kio_t kio_new(kio_dir_t dir, void *base, size_t len) {
  return (kio_t) {
    .dir = dir,
    .size = len,
    .buf = {
      .base = base,
      .len = len,
      .off = 0,
    },
  };
}

static inline kio_t kio_new_writeonly(void *base, size_t len) {
  return (kio_t) {
    .dir = KIO_IN,
    .size = len,
    .buf = {
      .base = (void *) base,
      .len = len,
      .off = 0,
    },
  };
}

static inline kio_t kio_new_readonly(const void *base, size_t len) {
  return (kio_t) {
    .dir = KIO_OUT,
    .size = len,
    .buf = {
      .base = (void *) base,
      .len = len,
      .off = 0,
    },
  };
}

size_t kio_transfered(const kio_t *kio);
size_t kio_remaining(const kio_t *kio);

size_t kio_copy(kio_t *dst, kio_t *src);
size_t kio_read(kio_t *kio, void *buf, size_t len, size_t off);
size_t kio_write(kio_t *kio, const void *buf, size_t len, size_t off);
size_t kio_fill(kio_t *kio, uint8_t byte, size_t len);

static inline size_t kio_readb(kio_t *kio, uint8_t *byte) { return kio_read(kio, byte, 1, 0); }
static inline size_t kio_writeb(kio_t *kio, uint8_t byte) { return kio_write(kio, &byte, 1, 0); }
static inline size_t kio_remfill(kio_t *kio, uint8_t byte) { return kio_fill(kio, byte, kio_remaining(kio)); }

#endif

#ifdef __PRINTF__
#ifndef KIO_PRINTF
#define KIO_PRINTF

static size_t kio_sprintf(kio_t *kio, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  size_t len = kvsnprintf(kio->buf.base + kio->buf.off, kio_remaining(kio), fmt, args);
  va_end(args);
  kio->buf.off += len;
  return len;
}

#endif
#endif
