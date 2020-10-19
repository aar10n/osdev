//
// Created by Aaron Gill-Braun on 2020-10-01.
//

#ifndef INCLUDE_BASE_H
#define INCLUDE_BASE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <boot.h>

typedef uint64_t clock_t;

//
// General Definitions
//

#define PAGE_SIZE 0x1000

#define MS_PER_SEC 1000
#define US_PER_SEC 1000000
#define NS_PER_SEC 1000000000
#define FS_PER_SEC 1000000000000000

//
// General Macros
//

#define static_assert(expr) _Static_assert(expr, "")

#define align(v, a) ((v) + (((a) - (v)) & ((a) - 1)))
#define align_ptr(p, a) ((void *) (align((uintptr_t)(p), (a))))

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))
#define abs(a) (((a) < 0) ? (-(a)) : (a))
#define diff(a, b) abs(a - b)
#define udiff(a, b) (max(a, b) - min(a, b))

#define label(lbl) lbl: NULL

#define barrier() \
  asm volatile("":::"memory");

#define cpu_pause() \
  asm volatile("pause":::"memory");

//
// GCC Extension Shorthands
//

#define noreturn _Noreturn
#define packed __attribute((packed))
#define noinline __attribute((noinline))
#define always_inline inline __attribute((always_inline))
#define weak __attribute((weak))
#define _unused __attribute((unused))
#define _used __attribute((used))
#define likely(expr) __builtin_expect((expr), 1)
#define unlikely(expr) __builtin_expect((expr), 0)

#define constfn __attribute((const))
#define coldfn __attribute((cold))
#define hotfn __attribute((hot))
#define interruptfn __attribute((interrupt))
#define flattenfn __attribute((flatten))
#define purefn __attribute((pure))

#define deprecated __attribute((deprecated))
#define aligned(val) __attribute((aligned(val)))
#define ifunc(resolver) __attribute((ifunc(resolver)))
#define malloc_like __attribute((malloc))
#define printf_like(i, j) __attribute((format(printf, i, j)))
#define section(name) __attribute((section(name)))
#define visibility(level) __attribute((visibility(level)))
#define warn_unused_result __attribute((warn_unused_result))


extern uintptr_t kernel_phys;
extern boot_info_t *boot_info;

#endif
