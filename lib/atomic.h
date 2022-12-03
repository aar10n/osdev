//
// Created by Aaron Gill-Braun on 2020-10-20.
//

#ifndef LIB_ATOMIC_H
#define LIB_ATOMIC_H

#include <base.h>
#include <asm/atomic.h>

#define atomic_fetch_add(ptr, val)  \
  (_Generic((*ptr),                 \
    uint8_t: __atomic_fetch_add8,   \
    uint16_t: __atomic_fetch_add16, \
    uint32_t: __atomic_fetch_add32, \
    uint64_t: __atomic_fetch_add64  \
  )(ptr, val))

#define atomic_fetch_sub(ptr, val) \
  __sync_fetch_and_sub(ptr, val)

#define atomic_bit_test_and_set(ptr, b) \
  __atomic_bit_test_and_set((void *)(ptr), b)

#define atomic_bit_test_and_reset(ptr, b) \
  __atomic_bit_test_and_reset((void *)(ptr), b)

#define atomic_lock_test_and_set(ptr) \
  __sync_lock_test_and_set(ptr, 1)
#define atomic_lock_test_and_reset(ptr) \
  __sync_lock_release(ptr)

#define atomic_cmpxchg(ptr, val) __atomic_cmpxchg64((uint64_t *)(ptr), (uintptr_t) val)

#endif
