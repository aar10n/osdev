//
// Created by Aaron Gill-Braun on 2023-02-17.
//

#ifndef KERNEL_KIO_H
#define KERNEL_KIO_H

#include <base.h>
#include <abi/iov.h>
#include <panic.h>

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

/// Return the number of bytes transfered.
size_t kio_transfered(const kio_t *kio);
/// Return the number of bytes remaining in the transfer.
size_t kio_remaining(const kio_t *kio);
/// Move data from the buffer at the given offset into the kio. Returns the number of bytes moved.
size_t kio_movein(kio_t *kio, const void *buf, size_t len, size_t off);
/// Move data from the kio into the buffer at the given offset. Returns the number of bytes moved.
size_t kio_moveout(kio_t *kio, void *buf, size_t len, size_t off);
/// Move a byte into the kio.
size_t kio_moveinb(kio_t *kio, uint8_t byte);
/// Move a byte out of the kio.
size_t kio_moveoutb(kio_t *kio, uint8_t *byte);

#endif
