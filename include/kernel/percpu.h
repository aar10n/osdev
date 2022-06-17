//
// Created by Aaron Gill-Braun on 2020-10-04.
//


#ifndef KERNEL_CPU_PERCPU_H
#define KERNEL_CPU_PERCPU_H
#define __PERCPU___

// Since this file is included in `base.h`, we run into
// problems if we also include `base.h` in this file.
// The include guards should allow us to do it but for
// some reason it isn't working... so this file must not
// include anything that includes `base.h`.

#include <abi/types.h>

#define PERCPU_SIZE (4096 * 4)

struct thread;
struct process;
struct vm;
struct scheduler;
struct idt;

typedef struct {
  uint64_t self;           // address of this struct
  uint64_t id;             // this cpu's id
  struct thread *thread;   // current thread
  struct process *process; // current process
  uint64_t kernel_sp;      // kernel stack pointer
  uint64_t user_sp;        // user stack pointer
  uint64_t rflags;         // cpu flags (on cli)
  int errno;
  uid_t uid;
  gid_t gid;
  struct scheduler *scheduler;
  struct idt *idt;
  struct vm *vm;
} percpu_t;
_Static_assert(sizeof(percpu_t) <= 4096, "");

// https://github.com/a-darwish/cuteOS
#define __percpu(var) (((percpu_t *) NULL)->var)
#define __percpu_type(var)	typeof(__percpu(var))
#define __percpu_marker(var) ((volatile __percpu_type(var) *)&__percpu(var))
#define __percpu_set(suffix, var, val) \
  ({                         \
    asm ("mov" suffix " %1, %%gs:%0" : \
         "=m" (*__percpu_marker(var)) : "ir" (val)); \
  })

#define percpu_get(var) \
  ({                    \
    __percpu_type(var) v; \
    asm("mov %%gs:%1, %0" : "=r" (v) : "m" (*__percpu_marker(var))); \
    v; \
  })

#define percpu_set(var, val) \
  _Generic((__percpu(var)),\
    int8_t: __percpu_set("b", var, val), uint8_t: __percpu_set("b", var, val), \
    int16_t: __percpu_set("w", var, val), uint16_t: __percpu_set("w", var, val), \
    int32_t: __percpu_set("l", var, val), uint32_t: __percpu_set("l", var, val), \
    int64_t: __percpu_set("q", var, val), uint64_t: __percpu_set("q", var, val), \
    default: __percpu_set("q", var, val))

#define percpu_ptr(var) \
  ({                     \
    uintptr_t p = percpu_get(self) + offsetof(percpu_t, var); \
    ((__percpu_type(var) *) p); \
  })

#define percpu_struct() \
  ((percpu_t *) percpu_get(self))

// encourage the compiler to heavily cache the value
static inline __attribute((always_inline)) __attribute((pure)) percpu_t *percpu() {
  uintptr_t ptr = percpu_get(self);
  return (percpu_t *) ptr;
}

#define PERCPU (percpu())
#define ID (percpu()->id)
#define IS_BSP (PERCPU->id == boot_info->bsp_id)
// #define current (percpu()->current)
#define current_process (percpu()->process)
#define current_thread (percpu()->thread)


void percpu_init();

#endif
