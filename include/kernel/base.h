//
// Created by Aaron Gill-Braun on 2020-10-01.
//

#ifndef INCLUDE_BASE_H
#define INCLUDE_BASE_H

#include <abi/types.h>
#include <boot.h>
#include <percpu.h>
#include <errno.h>

//
// General Definitions
//

#define KERNEL_PA      0x100000
#define KERNEL_OFFSET  0xFFFFFF8000000000
#define KERNEL_VA      0xFFFFFF8000100000
#define STACK_VA       0xFFFFFFA000000000
#define FRAMEBUFFER_VA 0xFFFFFFC000000000
#define MMIO_BASE_VA   0xFFFFFFFFE0000000
#define PROGRAM_VA     0xFFFFFF8000010000

#define SMPBOOT_START  0x0000
#define SMPDATA_START  0x1000

#define STACK_SIZE      0x4000 // 8 KiB
#define KERNEL_RESERVED 0x300000 // 3 MiB
#define RESERVED_TABLES 6 // Number of preallocated page tables

#define PAGE_SIZE 0x1000

#define KERNEL_CS 0x08ULL
#define USER_DS   0x18ULL
#define USER_CS   0x20ULL

#define MS_PER_SEC 1000
#define US_PER_SEC 1000000
#define NS_PER_SEC 1000000000
#define FS_PER_SEC 1000000000000000

//
// General Macros
//

#define static_assert(expr) _Static_assert(expr, "")

#define offset_ptr(p, c) ((void *)(((uint8_t *)(p)) + (c)))
#define align(v, a) ((v) + (((a) - (v)) & ((a) - 1)))
#define align_ptr(p, a) ((void *) (align((uintptr_t)(p), (a))))
#define ptr_after(s) ((void *)(((uintptr_t)(s)) + (sizeof(*s))))

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))
#define abs(a) (((a) < 0) ? (-(a)) : (a))
#define diff(a, b) abs((a) - (b))
#define udiff(a, b) (max(a, b) - min(a, b))

#define label(lbl) lbl: NULL

#define barrier() __asm volatile("":::"memory");
#define cpu_pause() __asm volatile("pause":::"memory");
#define cpu_hlt() __asm volatile("hlt")

#define bswap16(v) __builtin_bswap16(v)
#define bswap32(v) __builtin_bswap32(v)
#define bswap64(v) __builtin_bswap64(v)
#define bswap128(v) __builtin_bswap128(v)

#define big_endian(v) \
  _Generic(v,       \
    uint16_t: __builtin_bswap16(v), \
    uint32_t: __builtin_bswap32(v), \
    uint64_t: __builtin_bswap64(v) \
  )

//
// Compiler Attributes
//

#define noreturn _Noreturn
#define packed __attribute((packed))
#define noinline __attribute((noinline))
#define always_inline inline __attribute((always_inline))
#define __aligned(val) __attribute((aligned(val)))
#define deprecated __attribute((deprecated))
#define warn_unused_result __attribute((warn_unused_result))

#define __weak __attribute((weak))
#define __unused __attribute((unused))
#define __used __attribute((used))
#define __likely(expr) __builtin_expect((expr), 1)
#define __unlikely(expr) __builtin_expect((expr), 0)

#define __cold __attribute((cold))
#define __hot __attribute((hot))
#define __interrupt __attribute((interrupt))
#define __flatten __attribute((flatten))
#define __pure __attribute((pure))

#define __ifunc(resolver) __attribute((ifunc(resolver)))
#define __malloc_like __attribute((malloc))
#define __printf_like(i, j) __attribute((format(printf, i, j)))
#define __section(name) __attribute((section(name)))


extern boot_info_t *boot_info;

#endif
