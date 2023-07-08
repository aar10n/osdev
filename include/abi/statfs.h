//
// Created by Aaron Gill-Braun on 2023-07-07.
//

#ifndef INCLUDE_ABI_STATFS_H
#define INCLUDE_ABI_STATFS_H

typedef struct __fsid_t {
  int __val[2];
} fsid_t;

#include <bits/statfs.h>

#endif
