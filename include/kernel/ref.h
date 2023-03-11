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

typedef atomic_t refcount_t;

typedef struct kref {
  refcount_t refcount;
} kref_t;

static inline void kref_init(kref_t *kref) {
  atomic_init(&kref->refcount, 1);
}

static inline void kref_get(kref_t *kref) {
  atomic_inc(&kref->refcount);
}

static inline int kref_put(kref_t *kref, void (*release)(kref_t *)) {
  if (atomic_dec_and_test(&kref->refcount)) {
    release(kref);
    return 1;
  }
  return 0;
}

#endif
