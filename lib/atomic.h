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

#define atomic_bit_test_and_set(ptr) \
  __atomic_bit_test_and_set((uint8_t *)(ptr))

#define atomic_bit_test_and_reset(ptr) \
  __atomic_bit_test_and_reset((uint8_t *)(ptr))


#endif
