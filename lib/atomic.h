//
// Created by Aaron Gill-Braun on 2020-10-20.
//

#ifndef LIB_ATOMIC_H
#define LIB_ATOMIC_H

#include <base.h>
#include "asm/atomic.h"

#define atomic_fetch_add(ptr, val)  \
  (_Generic((*ptr),                 \
    uint8_t: __atomic_fetch_add8,   \
    uint16_t: __atomic_fetch_add16, \
    uint32_t: __atomic_fetch_add32, \
    uint64_t: __atomic_fetch_add64  \
  )(ptr, val))

#define test(ptr, val) \
  _Generic((ptr),\
    uint8_t*: 0, \
    uint16_t*: 1, \
    uint32_t*: 2, \
    uint64_t*: 3, \
    default: 4\
  )



#endif
