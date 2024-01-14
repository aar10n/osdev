//
// Created by Aaron Gill-Braun on 2023-02-20.
//

#ifndef KERNEL_REF_H
#define KERNEL_REF_H

#include <kernel/base.h>
#include <kernel/atomic.h>

// https://www.open-std.org/JTC1/SC22/WG21/docs/papers/2007/n2167.pdf

#define __move // identifies a pointer as a moved reference (ownership transferred)
#define __ref  // marks a pointer as a reference (ownership not transferred)

typedef volatile int refcount_t;

static inline void ref_init(refcount_t *ref) {
  *ref = 1;
}

static inline void ref_get(refcount_t *ref) { // NOLINT(*-non-const-parameter)
  atomic_fetch_add(ref, 1);
}

static inline int ref_put(refcount_t *ref) { // NOLINT(*-non-const-parameter)
  if (atomic_fetch_sub(ref, 1) == 0)
    return 1;
  return 0;
}

static inline int ref_count(const refcount_t *ref) {
  return *ref;
}


#define _refname refcount
#define _refcount refcount_t _refname
#define read_refcount(objptr) (ref_count(&(objptr)->_refname))

#define initref(objptr) (ref_init(&(objptr)->_refname))
#define newref(objptr) ({ ref_init(&(objptr)->_refname); objptr; })
#define getref(objptr) ({ if (__expect_true((objptr) != NULL)) ref_get(&(objptr)->_refname); objptr; })
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
