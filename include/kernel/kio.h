//
// Created by Aaron Gill-Braun on 2023-02-17.
//

#ifndef KERNEL_KIO_H
#define KERNEL_KIO_H

#include <base.h>
#include <abi/iov.h>

// kernel i/o transfer structure
struct kio {
  struct iovec *iov;  // io vector list
  int count;          // number of io vectors
  size_t offset;      // offset into target
  size_t resid;       // bytes remaining to transfer
  //
  uint32_t user : 1;  // user or kernel
  uint32_t write : 1; // read or write
  uint32_t : 30;
};

// NOTE: all kio functions are safe to call multiple times to incrementally
//       transfer data to or from a kio.

/// Return the total size of data held by the kio.
size_t kio_size(struct kio *kio);
/// Move data from the buffer into the kio. Returns the number of bytes left.
size_t kio_movein(struct kio *kio, const void *buf, size_t len);
/// Move data from the kio into the buffer. Returns the number of bytes left.
size_t kio_moveout(struct kio *kio, void *buf, size_t len);
/// Move a byte into the kio.
size_t kio_moveinb(struct kio *kio, uint8_t byte);
/// Move a byte out of the kio.
size_t kio_moveoutb(struct kio *kio, uint8_t *byte);

#endif
