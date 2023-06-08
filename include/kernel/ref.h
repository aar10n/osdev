//
// Created by Aaron Gill-Braun on 2023-02-20.
//

#ifndef KERNEL_REF_H
#define KERNEL_REF_H

#include <base.h>
#include <atomic.h>
#include <mutex.h>
#include <spinlock.h>

// implementation taken from Linux kernel
// https://www.open-std.org/JTC1/SC22/WG21/docs/papers/2007/n2167.pdf

#define __move // identifies a pointer as moved reference (ownership transferred)
#define __ref  // marks a pointer as a reference (ownership not transferred)

typedef atomic_t refcount_t;

static inline void ref_init(refcount_t *ref) {
  atomic_init(ref, 1);
}

static inline void ref_get(refcount_t *ref) {
  atomic_inc(ref);
}

static inline int ref_put(refcount_t *ref, void (*release)(refcount_t *)) {
  if (atomic_dec_and_test(ref)) {
    if (release) release(ref);
    return 1;
  }
  return 0;
}

static inline int ref_count(refcount_t *ref) {
  return atomic_read(ref);
}

#endif
