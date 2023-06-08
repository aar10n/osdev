//
// Created by Aaron Gill-Braun on 2022-12-22.
//

#ifndef INCLUDE_ABI_IOV_H
#define INCLUDE_ABI_IOV_H

#include "types.h"

/// Defines the structure of an I/O vector.
typedef struct iovec {
  void *iov_base;
  size_t iov_len;
} iovec_t;

#endif
