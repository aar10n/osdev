//
// Created by Aaron Gill-Braun on 2020-10-01.
//

#ifndef INCLUDE_BASE_H
#define INCLUDE_BASE_H

#include <kernel/types.h>
#include <bits/errno.h>
#include <bits/syscall.h>

#define __PERCPU_BASE__
#include <kernel/percpu.h>
#undef __PERCPU_BASE__

#include <boot.h>
#include <limits.h>
#include <macros.h>

#define __in      // identifies a pointer parameter as an input parameter
#define __out     // identifies a pointer parameter as an output parameter
#define __inout   // identifies a pointer parameter as an input/output parameter (modified)
#define __locked  // identifies a parameter or return as being locked
#define __unlocked  // identifies a parameter or return as being unlocked

//
// General Definitions
//

#define MS_PER_SEC 1000LL
#define US_PER_SEC 1000000LL
#define NS_PER_SEC 1000000000LL
#define NS_PER_MSEC 1000000
#define NS_PER_USEC 1000
#define FS_PER_SEC 1000000000000000

#define SEC_TO_NS(sec) ((uint64_t)(sec) * NS_PER_SEC)
#define MS_TO_NS(ms) ((uint64_t)(ms) * (NS_PER_SEC / MS_PER_SEC))
#define US_TO_NS(us) ((uint64_t)(us) * (NS_PER_SEC / US_PER_SEC))
#define FS_TO_NS(fs) ((uint64_t)(fs) / (FS_PER_SEC / NS_PER_SEC))
#define MS_TO_US(ms) ((uint64_t)(ms) * (US_PER_SEC / MS_PER_SEC))

#define SIZE_1KB  0x400ULL
#define SIZE_2KB  0x800ULL
#define SIZE_4KB  0x1000ULL
#define SIZE_8KB  0x2000ULL
#define SIZE_16KB 0x4000ULL
#define SIZE_1MB  0x100000ULL
#define SIZE_2MB  0x200000ULL
#define SIZE_4MB  0x400000ULL
#define SIZE_8MB  0x800000ULL
#define SIZE_16MB 0x1000000ULL
#define SIZE_1GB  0x40000000ULL
#define SIZE_2GB  0x80000000ULL
#define SIZE_4GB  0x100000000ULL
#define SIZE_8GB  0x200000000ULL
#define SIZE_16GB 0x400000000ULL
#define SIZE_1TB  0x10000000000ULL

//
// General Macros
//

#define static_assert(expr) _Static_assert(expr, "")

#define offset_ptr(p, c) ((void *)(((uintptr_t)(p)) + (c)))
#define offset_addr(p, c) (((uintptr_t)(p)) + (c))
#define align(v, a) ((v) + (((a) - (v)) & ((a) - 1)))
#define align_down(v, a) ((v) & ~((a) - 1))
#define page_align(v) align(v, PAGE_SIZE)
#define page_trunc(v) align_down(v, PAGE_SIZE)
#define is_aligned(v, a) (((v) & ((a) - 1)) == 0)
#define is_pow2(v) (((v) & ((v) - 1)) == 0)
#define prev_pow2(v) (1 << ((sizeof(v)*8 - 1) - __builtin_clz(v)))
#define next_pow2(v) (1 << ((sizeof(v)*8 - __builtin_clz((v) - 1))))
#define align_ptr(p, a) ((void *) (align((uintptr_t)(p), (a))))
#define ptr_after(s) ((void *)(((uintptr_t)(s)) + (sizeof(*(s)))))

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))
#define abs(a) (((a) < 0) ? (-(a)) : (a))
#define diff(a, b) abs((a) - (b))
#define udiff(a, b) (max(a, b) - min(a, b))

#define moveptr(objptr) ({ typeof(objptr) __tmp = (objptr); (objptr) = NULL; __tmp; })

#define LABEL(l) l: NULL

#define barrier() __asm volatile("":::"memory");
#define cpu_pause() __asm volatile("pause":::"memory");
#define cpu_hlt() __asm volatile("hlt")
#define WHILE_TRUE ({ while (true) cpu_pause(); })

#define bswap16(v) __builtin_bswap16(v)
#define bswap32(v) __builtin_bswap32(v)
#define bswap64(v) __builtin_bswap64(v)
#define bswap128(v) __builtin_bswap128(v)

#define big_endian(v) \
  _Generic(v,       \
    uint16_t: bswap16(v), \
    uint32_t: bswap32(v), \
    uint64_t: bswap64(v) \
  )

#define container_of(ptr, type, member) ({ \
    const typeof(((type *)0)->member) *__mptr = (ptr); \
    (type *)((char *)__mptr - offsetof(type, member)); \
  })

#define SIGNATURE_16(A, B) ((A) | ((B) << 8))
#define SIGNATURE_32(A, B, C, D) (SIGNATURE_16(A, B) | (SIGNATURE_16(C, D) << 16))
#define SIGNATURE_64(A, B, C, D, E, F, G, H) (SIGNATURE_32(A, B, C, D) | ((uint64_t) SIGNATURE_32(E, F, G, H) << 32))

#define ASSERT_IS_TYPE(type, value) \
  _Static_assert(_Generic(value, type: 1, default: 0) == 1, \
  "Failed type assertion: " #value " is not of type " #type)

#define ASSERT_IS_TYPE_OR_CONST(type, value) \
  _Static_assert(_Generic(value, type: 1, const type: 1, default: 0) == 1, \
  "Failed type assertion: " #value " is not of type " #type)

#define __type_checked(type, param, rest) ({ ASSERT_IS_TYPE(type, param); rest; })
#define __const_type_checked(type, param, rest) ({ ASSERT_IS_TYPE_OR_CONST(type, param); rest; })

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

//
// Compiler Attributes
//

#define noreturn _Noreturn
#define packed __attribute((packed))
#define noinline __attribute((noinline))
#define always_inline inline __attribute((always_inline))
#define __aligned(val) __attribute((aligned((val))))
#define deprecated __attribute((deprecated))
#define warn_unused_result __attribute((warn_unused_result))

#define weak __attribute((weak))
#define unused __attribute((unused))
#define alias(name) __attribute((alias(name)))
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
#define __sentinel(n) __attribute((sentinel(n)))
#define __nonnull(...) __attribute((nonnull(__VA_ARGS__)))

#define __expect_true(expr) __builtin_expect((expr), 1)
#define __expect_false(expr) __builtin_expect((expr), 0)

#define todo(msg) ({ kprintf("TODO: %s:%d: %s\n", __FILE__, __LINE__, #msg); WHILE_TRUE; })

//
// Special Macros
//

/**
 * The LOAD_SECTION macro provides a mechanism to load arbitrary sections of the
 * kernel image at boot time. The bootloader will go through all symbols declared
 * with this macro, attempt to load each section and then update the corresponding
 * struct to hold the physical/virtual address of the section and its length.
 *
 * If a section is requested but it has already been loaded in during the normal
 * elf loading procedure, the struct will point to the virtual address of where it
 * was mapped to. Otherwise, it will be placed in an unoccupied section of memory
 * and the struct will contain the physical address of where it was placed. It is
 * up to the kernel to later map these sections into virtual memory.
 */
#define LOAD_SECTION(varname, secname) loaded_section_t __attribute__((section(".load_sections"))) varname = { .name = secname }

/**
 * The EARLY_INIT macro provides a way to register initializer functions that are invoked
 * at the end of the 'early' phase. These functions may only use panic, kmalloc and other
 * 'early' APIs, and they are not called from within a thread context.
 */
#define EARLY_INIT(fn) static __attribute__((section(".init_array.early"))) void (*__do_early_init_ ## fn)() = fn

/**
 * The PERCPU_EARLY_INIT macro provides a way to register initializer functions that are
 * invoked by each CPU at the end of the 'early' phase. The same restrictions apply as
 * with EARLY_INIT functions. On the boot CPU, these functions are called after the
 * normal 'early' initializers.
 */
#define PERCPU_EARLY_INIT(fn) static __attribute__((section(".init_array.percpu"))) void (*__do_percpu_init_ ## fn)() = fn

/**
 * The STATIC_INIT macro provides a way to register initializer functions that are invoked
 * at the end of the 'static' phase. These functions may only use the memory, time, and
 * irq APIs, and are called from within the proc0 context.
 */
#define STATIC_INIT(fn) static __attribute__((section(".init_array.static"))) void (*__do_static_init_ ## fn)() = fn

/**
 * The MODULE_INIT macro provides a way to register initializer functions that are invoked
 * from within the root process and have access to all kernel APIs.
 */
#define MODULE_INIT(fn) static __attribute__((section(".init_array.module"))) void (*__do_module_init_ ## fn)() = fn

/**
 * Defines a system call function. This has the effect of overriding the default stub.
 */
#define DEFINE_SYSCALL(name, ret_type, ...) \
  static_assert(SYS_ ##name >= 0); \
  ret_type sys_ ##name(MACRO_JOIN(__VA_ARGS__))

/**
 * Defines a system call as an alias to an existing function.
 */
#define SYSCALL_ALIAS(name, symbol_name) \
  static_assert(SYS_ ##name >= 0);            \
  __typeof(symbol_name) sys_ ##name __attribute__((__alias__(#symbol_name)))


//
// Global Symbols
//

extern boot_info_v2_t *boot_info_v2;
extern uint32_t system_num_cpus;
extern bool is_smp_enabled;
extern bool is_debug_enabled;

// linker provided symbols
extern uintptr_t __kernel_address;
extern uintptr_t __kernel_virtual_offset;
extern uintptr_t __kernel_code_start;
extern uintptr_t __kernel_code_end;
extern uintptr_t __kernel_data_end;

#ifndef __PRINTF__
void kprintf(const char *format, ...);
#endif

//
// Debug Macros
//

#define QEMU_DEBUG_OUT_PORT   0x800
#define QEMU_DEBUG_OUTS_PORT  0x801
#define QEMU_DEBUG_PTR_PORT   0x808

#define QEMU_DEBUG_CHAR(c) __asm__ volatile("out dx, al" : : "a"(c), "d"(QEMU_DEBUG_OUT_PORT))
#define QEMU_DEBUG_CONST_STR(str) qemu_debug_string(str, sizeof(str))
#define QEMU_DEBUG_CHARP(str) qemu_debug_string(str, strlen(str)+1)
#define QEMU_DEBUG_PTR(ptr) ({ \
  __asm__ volatile("out dx, eax" : : "a"((ptr) & UINT32_MAX), "d"(QEMU_DEBUG_PTR_PORT)); \
  __asm__ volatile("out dx, eax" : : "a"(((ptr) >> 32) & UINT32_MAX), "d"(QEMU_DEBUG_PTR_PORT)); \
})

static inline void qemu_debug_string(const char *s, uint16_t len) {
  asm volatile (
    "rep outsb"
    : "+S"(s), "+c"(len)
    : "d"(QEMU_DEBUG_OUTS_PORT)
    : "memory"
  );
}

#endif
