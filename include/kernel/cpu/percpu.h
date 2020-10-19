//
// Created by Aaron Gill-Braun on 2020-10-04.
//

#ifndef KERNEL_CPU_PERCPU_H
#define KERNEL_CPU_PERCPU_H

#include <base.h>
#include <process.h>
#include <cpu/cpu.h>

#define PERCPU_RESERVED PAGE_SIZE

typedef struct {
  uint64_t apic_id;
  process_t *current;
  uintptr_t self;
} percpu_t;

static_assert(sizeof(percpu_t) <= PERCPU_RESERVED);

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

#define percpu_addr(var) \
  ({                     \
    uintptr_t p = percpu_get(self) + offsetof(percpu_t, var); \
    ((__percpu_type(var) *) p); \
  })

#define currentp (percpu_process())

static always_inline purefn process_t *percpu_process() {
  process_t *current;
  asm ("mov %%gs:0, %0" : "=r" (current));
  return current;
}


void percpu_init();
void percpu_init_cpu();

#endif
