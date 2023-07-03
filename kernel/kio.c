//
// Created by Aaron Gill-Braun on 2023-02-17.
//

#include <kernel/kio.h>
#include <kernel/panic.h>
#include <kernel/string.h>

#define ASSERT(x) kassert(x)

//

size_t kio_transfered(const kio_t *kio) {
  return kio->buf.off;
}

size_t kio_remaining(const kio_t *kio) {
  return kio->buf.len - kio->buf.off;
}

//

size_t kio_transfer(kio_t *dst, kio_t *src) {
  ASSERT(dst->dir == KIO_IN);
  ASSERT(src->dir == KIO_OUT);
  size_t remain = kio_remaining(dst);
  size_t to_copy = kio_remaining(src);
  if (to_copy > remain) {
    to_copy = remain;
  }

  if (to_copy > 0) {
    memcpy(dst->buf.base + dst->buf.off, src->buf.base + src->buf.off, to_copy);
    dst->buf.off += to_copy;
    src->buf.off += to_copy;
  }
  return to_copy;
}

size_t kio_nread_out(void *buf, size_t len, size_t off, size_t n, kio_t *kio) {
  ASSERT(kio->dir == KIO_OUT);
  size_t remain = kio_remaining(kio);
  if (off >= len) {
    return 0;
  }

  size_t to_copy = len - off;
  if (to_copy > remain) {
    to_copy = remain;
  }

  if (n > 0 && to_copy > n) {
    to_copy = n;
  }

  if (to_copy > 0) {
    memcpy(buf + off, kio->buf.base + kio->buf.off, to_copy);
    kio->buf.off += to_copy;
  }
  return to_copy;
}

size_t kio_nwrite_in(kio_t *kio, const void *buf, size_t len, size_t off, size_t n) {
  ASSERT(kio->dir == KIO_IN);
  size_t remain = kio_remaining(kio);
  if (off >= len) {
    return 0;
  }

  size_t to_copy = len - off;
  if (to_copy > remain) {
    to_copy = remain;
  }

  if (n > 0 && to_copy > n) {
    to_copy = n;
  }

  if (to_copy > 0) {
    memcpy(kio->buf.base + kio->buf.off, buf + off, to_copy);
    kio->buf.off += to_copy;
  }
  return to_copy;
}

size_t kio_fill(kio_t *kio, uint8_t byte, size_t len) {
  ASSERT(kio->dir == KIO_IN);
  size_t remain = kio_remaining(kio);
  if (len > remain) {
    len = remain;
  }

  if (len > 0) {
    memset(kio->buf.base + kio->buf.off, byte, len);
    kio->buf.off += len;
  }
  return len;
}
