//
// Created by Aaron Gill-Braun on 2023-02-17.
//

#include <kernel/kio.h>
#include <kernel/panic.h>
#include <kernel/string.h>

#define ASSERT(x) kassert(x)

//

size_t kio_transfered(const kio_t *kio) {
  if (kio->kind == KIO_BUF) {
    return kio->buf.off;
  } else if (kio->kind == KIO_IOV) {
    return kio->iov.t_off;
  } else {
    panic("invalid kio type");
  }
}

size_t kio_remaining(const kio_t *kio) {
  if (kio->kind == KIO_BUF) {
    return kio->size - kio->buf.off;
  } else if (kio->kind == KIO_IOV) {
    return kio->size - kio->iov.t_off;
  } else {
    panic("invalid kio type");
  }
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

  if (src->kind == KIO_BUF) {
    kio_nwrite_in(dst, src->buf.base, src->size, src->buf.off, to_copy);
    src->buf.off += to_copy;
  } else if (src->kind == KIO_IOV) {
    const struct iovec *iov = src->iov.arr;
    while (to_copy > 0) {
      size_t iov_remain = iov[src->iov.idx].iov_len - src->iov.off;
      if (to_copy <= iov_remain) {
        kio_nwrite_in(dst, iov[src->iov.idx].iov_base, iov[src->iov.idx].iov_len, src->iov.off, to_copy);
        src->iov.off += to_copy;
        if (to_copy == iov_remain) {
          src->iov.idx++;
          src->iov.off = 0;
        }
        break;
      } else {
        kio_nwrite_in(dst, iov[src->iov.idx].iov_base, iov[src->iov.idx].iov_len, src->iov.off, iov_remain);
        src->iov.off += iov_remain;
        to_copy -= iov_remain;
        src->iov.idx++;
      }
    }
  } else {
    panic("invalid kio type");
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
  if (to_copy == 0)
    return 0;

  if (kio->kind == KIO_BUF) {
    memcpy(buf + off, kio->buf.base + kio->buf.off, to_copy);
    kio->buf.off += to_copy;
  } else if (kio->kind == KIO_IOV) {
    const struct iovec *iov = kio->iov.arr;
    while (to_copy > 0) {
      size_t iov_remain = iov[kio->iov.idx].iov_len - kio->iov.off;
      if (to_copy <= iov_remain) {
        memcpy(buf + off, iov[kio->iov.idx].iov_base + kio->iov.off, to_copy);
        kio->iov.off += to_copy;
        kio->iov.t_off += to_copy;
        if (to_copy == iov_remain) {
          kio->iov.idx++;
          kio->iov.off = 0;
        }
        break;
      } else {
        memcpy(buf + off, iov[kio->iov.idx].iov_base + kio->iov.off, iov_remain);
        kio->iov.off += iov_remain;
        kio->iov.t_off += iov_remain;
        to_copy -= iov_remain;
        off += iov_remain;
        kio->iov.idx++;
      }
    }
  } else {
    panic("invalid kio type");
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
  if (to_copy == 0)
    return 0;

  if (kio->kind == KIO_BUF) {
    memcpy(kio->buf.base + kio->buf.off, buf + off, to_copy);
    kio->buf.off += to_copy;
  } else if (kio->kind == KIO_IOV) {
    const struct iovec *iov = kio->iov.arr;
    while (to_copy > 0) {
      size_t iov_remain = iov[kio->iov.idx].iov_len - kio->iov.off;
      if (to_copy <= iov_remain) {
        memcpy(iov[kio->iov.idx].iov_base + kio->iov.off, buf + off, to_copy);
        kio->iov.off += to_copy;
        kio->iov.t_off += to_copy;
        if (to_copy == iov_remain) {
          kio->iov.idx++;
          kio->iov.off = 0;
        }
        break;
      } else {
        memcpy(iov[kio->iov.idx].iov_base + kio->iov.off, buf + off, iov_remain);
        kio->iov.off += iov_remain;
        kio->iov.t_off += iov_remain;
        to_copy -= iov_remain;
        off += iov_remain;
        kio->iov.idx++;
      }
    }
  } else {
    panic("invalid kio type");
  }
  return to_copy;
}

size_t kio_fill(kio_t *kio, uint8_t byte, size_t len) {
  ASSERT(kio->dir == KIO_IN);
  size_t remain = kio_remaining(kio);
  if (len > remain) {
    len = remain;
  }
  if (len == 0)
    return 0;

  if (kio->kind == KIO_BUF) {
    memset(kio->buf.base + kio->buf.off, byte, len);
    kio->buf.off += len;
  } else if (kio->kind == KIO_IOV) {
    const struct iovec *iov = kio->iov.arr;
    while (len > 0) {
      size_t iov_remain = iov[kio->iov.idx].iov_len - kio->iov.off;
      if (len <= iov_remain) {
        memset(iov[kio->iov.idx].iov_base + kio->iov.off, byte, len);
        kio->iov.off += len;
        kio->iov.t_off += len;
        if (len == iov_remain) {
          kio->iov.idx++;
          kio->iov.off = 0;
        }
        break;
      } else {
        memset(iov[kio->iov.idx].iov_base + kio->iov.off, byte, iov_remain);
        kio->iov.off += iov_remain;
        kio->iov.t_off += iov_remain;
        len -= iov_remain;
        kio->iov.idx++;
      }
    }
  } else {
    panic("invalid kio type");
  }
  return len;
}
