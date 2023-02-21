//
// Created by Aaron Gill-Braun on 2023-02-17.
//

#include <kio.h>
#include <panic.h>
#include <string.h>

#define ASSERT(x) kassert(x)

size_t kio_size(struct kio *kio) {
  return kio->resid;
}

size_t kio_movein(struct kio *kio, const void *buf, size_t len) {
  ASSERT(!kio->write); // read
  for (int i = 0; i < kio->count && len > 0; i++) {
    if (kio->iov[i].iov_len == 0) {
      continue;
    }

    size_t iov_len = kio->iov[i].iov_len;
    if (len < iov_len) {
      iov_len = len;
    }
    memcpy(kio->iov[i].iov_base, buf, iov_len);
    buf += iov_len;
    len -= iov_len;
    kio->iov[i].iov_base += iov_len;
    kio->iov[i].iov_len -= iov_len;
    kio->resid -= iov_len;
  }
  return kio->resid;
}

size_t kio_moveout(struct kio *kio, void *buf, size_t len) {
  ASSERT(kio->write); // write
  for (int i = 0; i < kio->count && len > 0; i++) {
    if (kio->iov[i].iov_len == 0) {
      continue;
    }

    size_t iov_len = kio->iov[i].iov_len;
    if (len < iov_len) {
      iov_len = len;
    }
    memcpy(buf, kio->iov[i].iov_base, iov_len);
    buf += iov_len;
    len -= iov_len;
    kio->iov[i].iov_base += iov_len;
    kio->iov[i].iov_len -= iov_len;
    kio->resid -= iov_len;
  }

  return kio->resid;
}

size_t kio_moveinb(struct kio *kio, uint8_t byte) {
  return kio_movein(kio, &byte, 1);
}

size_t kio_moveoutb(struct kio *kio, uint8_t *byte) {
  return kio_moveout(kio, byte, 1);
}
