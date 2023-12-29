//
// Created by Aaron Gill-Braun on 2020-10-20.
//

#ifndef LIB_ATOMIC_H
#define LIB_ATOMIC_H

#include <asm/atomic.h>

#define atomic_fetch_add(ptr, val)  \
  __sync_fetch_and_add(ptr, val)

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

//

typedef struct {
  int _count;
} atomic_t;

static inline void atomic_init(atomic_t *ptr, int v) {
  ptr->_count = v;
}

static inline int atomic_read(const atomic_t *ptr) {
  return (*((volatile int *)(&(ptr->_count))));
}

static inline void atomic_set(atomic_t *ptr, int v) {
  *ptr = (atomic_t) { ._count = v };
}

static inline void atomic_inc(atomic_t *ptr) {
  __sync_fetch_and_add(&ptr->_count, 1);
}

static inline void atomic_dec(atomic_t *ptr) {
  __sync_fetch_and_sub(&ptr->_count, 1);
}

static inline int atomic_inc_and_test(atomic_t *ptr) {
  return __sync_add_and_fetch(&ptr->_count, 1) == 0;
}

static inline int atomic_dec_and_test(atomic_t *ptr) {
  return __sync_sub_and_fetch(&ptr->_count, 1) == 0;
}

static inline int atomic_cmpxchg(atomic_t *ptr, int oldv, int newv) {
  return __sync_bool_compare_and_swap(&ptr->_count, oldv, newv);
}



#endif
