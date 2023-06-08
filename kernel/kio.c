//
// Created by Aaron Gill-Braun on 2023-02-17.
//

#include <kio.h>
#include <panic.h>
#include <string.h>
#include <mm.h>

#define ASSERT(x) kassert(x)

//

size_t kio_transfered(const kio_t *kio) {
  return kio->buf.off;
}

size_t kio_remaining(const kio_t *kio) {
  return kio->buf.len - kio->buf.off;
}

//

size_t kio_copy(kio_t *dst, kio_t *src) {
  ASSERT(dst->dir == KIO_OUT);
  ASSERT(src->dir == KIO_IN);
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

size_t kio_movein(kio_t *kio, const void *buf, size_t len, size_t off) {
  ASSERT(kio->dir == KIO_IN);
  size_t remain = kio_remaining(kio);
  if (off > len) {
    return 0;
  }

  size_t to_copy = len - off;
  if (to_copy > remain) {
    to_copy = remain;
  }

  if (to_copy > 0) {
    memcpy(kio->buf.base + kio->buf.off, buf + off, to_copy);
    kio->buf.off += to_copy;
  }
  return to_copy;
}

size_t kio_moveout(kio_t *kio, void *buf, size_t len, size_t off) {
  ASSERT(kio->dir == KIO_OUT);
  size_t remain = kio_remaining(kio);
  if (off > len) {
    return 0;
  }

  size_t to_copy = len - off;
  if (to_copy > remain) {
    to_copy = remain;
  }

  if (to_copy > 0) {
    memcpy(buf + off, kio->buf.base + kio->buf.off, to_copy);
    kio->buf.off += to_copy;
  }
  return to_copy;
}

size_t kio_moveinb(kio_t *kio, uint8_t byte) {
  return kio_movein(kio, &byte, 1, 0);
}

size_t kio_moveoutb(kio_t *kio, uint8_t *byte) {
  return kio_moveout(kio, byte, 1, 0);
}
