//
// Created by Aaron Gill-Braun on 2023-02-20.
//

#ifndef KERNEL_REF_H
#define KERNEL_REF_H

#include <kernel/base.h>
#include <atomic.h>
#include <kernel/mutex.h>
#include <kernel/spinlock.h>

// implementation taken from Linux kernel
// https://www.open-std.org/JTC1/SC22/WG21/docs/papers/2007/n2167.pdf

#define __move // identifies a pointer as a moved reference (ownership transferred)
#define __ref  // marks a pointer as a reference (ownership not transferred)

typedef atomic_t refcount_t;

static inline void ref_init(refcount_t *ref) {
  atomic_init(ref, 1);
}

static inline void ref_get(refcount_t *ref) {
  atomic_inc(ref);
}

static inline int ref_put(refcount_t *ref) {
  if (atomic_dec_and_test(ref)) {
    return 1;
  }
  return 0;
}

static inline int ref_count(refcount_t *ref) {
  return atomic_read(ref);
}


#define _refname refcount
#define _refcount refcount_t _refname
#define read_refcount(objptr) (ref_count(&(objptr)->_refname))

#define initref(objptr) (ref_init(&(objptr)->_refname))
#define newref(objptr) ({ ref_init(&(objptr)->_refname); objptr; })
#define getref(objptr) ({ ref_get(&(objptr)->_refname); objptr; })
#define moveref(objref) ({ typeof(objref) tmp = (objref); (objref) = NULL; tmp; })
// putref(obj **objpptr, void(*objdtor)(obj*))
#define putref(objpptr, objdtor) ({ \
  if (*(objpptr)) { \
    if (ref_put(&((*(objpptr)))->_refname)) { \
      objdtor(*(objpptr)); \
    } \
    *(objpptr) = NULL; \
  } \
})
#define try_putref(objptr) ({ \
  int res = ref_put(&((objptr))->_refname); \
  (objptr) = NULL; \
  res; \
})

#endif
