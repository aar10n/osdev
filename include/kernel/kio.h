//
// Created by Aaron Gill-Braun on 2023-02-17.
//

#ifndef KERNEL_KIO_H
#define KERNEL_KIO_H
#define __KIO__

#include <kernel/base.h>
#include <kernel/panic.h>

struct iovec;

typedef enum kio_kind {
  KIO_BUF,
  KIO_IOV,
} kio_kind_t;

typedef enum kio_dir {
  KIO_IN,
  KIO_OUT,
} kio_dir_t;

/// Kernel I/O transfer structure.
///
/// The kio structure is used to represent a data transfer. It does not
/// own the underlying buffers.
typedef struct kio {
  kio_kind_t kind;              // the kind of transfer
  kio_dir_t dir;                // transfer direction
  size_t size;                  // total size of the transfer
  union {
    struct {
      void *base;               // buffer base address
      size_t off;               // current buffer offset
    } buf;
    struct {
      const struct iovec *arr;  // array of iovecs
      uint32_t cnt;             // number of iovecs
      uint32_t idx;             // current iovec index
      size_t off;               // current iovec offset
      size_t t_off;             // transfer offset
    } iov;
  };
} kio_t;

static inline kio_t kio_new_write(void *base, size_t len) {
  return (kio_t) {
    .kind = KIO_BUF,
    .dir = KIO_IN,
    .size = len,
    .buf = {
      .base = (void *) base,
      .off = 0,
    },
  };
}

static inline kio_t kio_new_read(const void *base, size_t len) {
  return (kio_t) {
    .kind = KIO_BUF,
    .dir = KIO_OUT,
    .size = len,
    .buf = {
      .base = (void *) base,
      .off = 0,
    },
  };
}

static inline kio_t kio_new_writev(const struct iovec *iov, uint32_t iovcnt) {
  size_t size = 0;
  for (uint32_t i = 0; i < iovcnt; i++) {
    size += iov[i].iov_len;
  }

  return (kio_t) {
    .kind = KIO_IOV,
    .dir = KIO_IN,
    .size = size,
    .iov = {
      .arr = iov,
      .cnt = iovcnt,
      .idx = 0,
      .off = 0,
      .t_off = 0,
    },
  };
}

static inline kio_t kio_new_readv(const struct iovec *iov, uint32_t iovcnt) {
  size_t size = 0;
  for (uint32_t i = 0; i < iovcnt; i++) {
    size += iov[i].iov_len;
  }

  return (kio_t) {
    .kind = KIO_IOV,
    .dir = KIO_OUT,
    .size = size,
    .iov = {
      .arr = iov,
      .cnt = iovcnt,
      .idx = 0,
      .off = 0,
      .t_off = 0,
    },
  };
}

size_t kio_transfered(const kio_t *kio);
size_t kio_remaining(const kio_t *kio);

size_t kio_transfer(kio_t *dst, kio_t *src);
size_t kio_nread_out(void *buf, size_t len, size_t off, size_t n, kio_t *kio);
size_t kio_nwrite_in(kio_t *kio, const void *buf, size_t len, size_t off, size_t n);
size_t kio_fill(kio_t *kio, uint8_t byte, size_t len);

static inline size_t kio_read_out(void *buf, size_t len, size_t off, kio_t *kio) { return kio_nread_out(buf, len, off, 0, kio); }
static inline size_t kio_write_in(kio_t *kio, const void *buf, size_t len, size_t off) { return kio_nwrite_in(kio, buf, len, off, 0); }
static inline size_t kio_read_ch(char *ch, kio_t *kio) { return kio_read_out(ch, 1, 0, kio); }
static inline size_t kio_write_ch(kio_t *kio, char ch) { return kio_write_in(kio, &ch, 1, 0); }
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
