//
// Created by Aaron Gill-Braun on 2022-12-22.
//

#ifndef KERNEL_IOV_H
#define KERNEL_IOV_H

#include <base.h>

/// Defines the structure of an I/O vector.
typedef struct iovec {
  void *iov_base;
  size_t iov_len;
} iovec_t;


static inline size_t iov_size(const struct iovec *iov, const unsigned int iov_cnt) {
  size_t size = 0;
  for (int i = 0; i < iov_cnt; i++) {
    size += iov[i].iov_len;
  }
  return size;
}


#endif
