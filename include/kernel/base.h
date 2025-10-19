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

#define MS_PER_SEC  1000LL
#define US_PER_SEC  1000000LL
#define US_PER_NS   1000LL
#define NS_PER_SEC  1000000000LL
#define NS_PER_MS   1000000LL
#define NS_PER_USEC 1000LL
#define FS_PER_SEC  1000000000000000LL

#define SEC_TO_NS(sec) ((uint64_t)(sec) * NS_PER_SEC)
#define MS_TO_NS(ms) ((uint64_t)(ms) * (NS_PER_SEC / MS_PER_SEC))
#define US_TO_NS(us) ((uint64_t)(us) * (NS_PER_SEC / US_PER_SEC))
#define FS_TO_NS(fs) ((uint64_t)(fs) / (FS_PER_SEC / NS_PER_SEC))
#define MS_TO_US(ms) ((uint64_t)(ms) * (US_PER_SEC / MS_PER_SEC))
#define NS_TO_MS(ns) ((uint64_t)(ns) / (NS_PER_SEC / MS_PER_SEC))
#define NS_TO_US(ns) ((uint64_t)(ns) / (NS_PER_SEC / US_PER_SEC))

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

// network byte order conversions (big endian)
#define htons(v) bswap16(v)
#define ntohs(v) bswap16(v)
#define htonl(v) bswap32(v)
#define ntohl(v) bswap32(v)

#define container_of(ptr, type, member) ({ \
    const typeof(((type *)0)->member) *__mptr = (ptr); \
    (type *)((char *)__mptr - offsetof(type, member)); \
  })

#define SIGNATURE_16(A, B) ((A) | ((B) << 8))
#define SIGNATURE_32(A, B, C, D) (SIGNATURE_16(A, B) | (SIGNATURE_16(C, D) << 16))
#define SIGNATURE_64(A, B, C, D, E, F, G, H) (SIGNATURE_32(A, B, C, D) | ((uint64_t) SIGNATURE_32(E, F, G, H) << 32))

#pragma clang diagnostic push
#pragma ide diagnostic ignored "bugprone-macro-parentheses"

#define ASSERT_IS_TYPE(type, value) \
  _Static_assert(_Generic(value, type: 1, default: 0) == 1, \
  "Failed type assertion: " #value " is not of type " #type)

#define ASSERT_IS_TYPE_OR_CONST(type, value) \
  _Static_assert(_Generic(value, type: 1, const type: 1, default: 0) == 1, \
  "Failed type assertion: " #value " is not of type " #type)

#pragma clang diagnostic pop

#define __type_checked(type, param, rest) ({ ASSERT_IS_TYPE(type, param); rest; })
#define __const_type_checked(type, param, rest) ({ ASSERT_IS_TYPE_OR_CONST(type, param); rest; })

// this doesnt need to handle char and short because they get promoted to int in _Generic
#define CORE_TYPE_TO_STRING(x) _Generic((x), \
    int: "int",                         \
    unsigned int: "unsigned int",       \
    long: "long",                       \
    unsigned long: "unsigned long",     \
    float: "float",                     \
    double: "double",                   \
    char *: "char *",                   \
    const char *: "const char *",       \
    default: "unknown")

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define goto_res(lbl, err) do { res = err; goto lbl; } while (0)

//
// Compiler Attributes
//

#define noreturn _Noreturn
#define packed __attribute((packed))
#define noinline __attribute((noinline))
#define always_inline inline __attribute((always_inline))
#define deprecated __attribute((deprecated))
#define warn_unused_result __attribute((warn_unused_result))
#define _aligned(val) __attribute((aligned((val))))

#define _alias(name) __attribute((alias(name)))
#define _weak __attribute((weak))
#define _used __attribute((used))
#define _unused __attribute((unused))
#define _likely(expr) __builtin_expect((expr), 1)
#define _unlikely(expr) __builtin_expect((expr), 0)

#define _ifunc(resolver) __attribute((ifunc(resolver)))
#define _malloc_like __attribute((malloc))
#define _printf_like(i, j) __attribute((format(printf, i, j)))
#define _fmt_like(i, j) __attribute__((annotate("fmt_format:" #i ":" #j)))
#define _section(name) __attribute((section(name)))
#define _sentinel(n) __attribute((sentinel(n)))
#define _nonnull(...) __attribute((nonnull(__VA_ARGS__)))

#define __expect_true(expr) __builtin_expect((expr), 1)
#define __expect_false(expr) __builtin_expect((expr), 0)

//
// Other Assertion/Safety Macros
//

#define todo(msg, ...) ({ panic("TODO: %s:%d: " msg "\n", __FILE__, __LINE__, ##__VA_ARGS__); })

#define __assert_stack_is_aligned() ({ \
  uintptr_t sp; \
  __asm__ __volatile__("mov %0, rsp" : "=r"(sp)); \
  if (sp & 0xF) { \
    kprintf_kputs("!!! stack pointer is not aligned to 16 bytes !!!\n"); \
    kprintf_kputs(__FILE__); \
    kprintf_kputs(":"); \
    kprintf_kputl(__LINE__); \
    kprintf_kputs("\n"); \
    WHILE_TRUE; \
  } \
})

#define add_checked_overflow(x, v) ({ \
    typeof(x) _x = (x); \
    typeof(x) _v = (v);                 \
    if (__builtin_add_overflow(_x, _v, &_x)) { \
      panic("add_checked_overflow: overflow detected (<%s>%llu + <%s>%llu) [%s:%d]", \
            CORE_TYPE_TO_STRING(x), (unsigned long)_x, \
            CORE_TYPE_TO_STRING(v), (unsigned long)_v, \
            __FILE__, __LINE__); \
    } \
    _x; \
  })

#define sub_checked_overflow(x, v) ({ \
    typeof(x) _x = (x); \
    typeof(x) _v = (v); \
    if (__builtin_sub_overflow(_x, _v, &_x)) { \
      panic("sub_checked_overflow: overflow detected (<%s>%lu - <%s>%lu) [%s:%d]", \
            CORE_TYPE_TO_STRING(x), (unsigned long)_x, \
            CORE_TYPE_TO_STRING(v), (unsigned long)_v, \
            __FILE__, __LINE__); \
    } \
    _x; \
  })


//
// Initializer Function Macros
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
#define PERCPU_EARLY_INIT(fn) static __attribute__((section(".init_array.early_percpu"))) void (*__do_percpu_early_init_ ## fn)() = fn

/**
 * The STATIC_INIT macro provides a way to register initializer functions that are invoked
 * at the end of the 'static' phase. These functions may only use the memory, time, and
 * irq APIs, and are called from within the proc0 context.
 */
#define STATIC_INIT(fn) static __attribute__((section(".init_array.static"))) void (*__do_static_init_ ## fn)() = fn

/**
 * The PERCPU_STATIC_INIT macro provides a way to register initializer functions that are
 * invoked by each CPU at the end of the 'static' phase. The same restrictions apply as
 * with STATIC_INIT functions.
 */
#define PERCPU_STATIC_INIT(fn) static __attribute__((section(".init_array.static_percpu"))) void (*__do_percpu_static_init_ ## fn)() = fn

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
void kprintf_kputs(const char *str);
void kprintf_kputl(long l);
#endif
#ifndef __PANIC__
noreturn void panic(const char *fmt, ...);
#endif

//
// Debug Macros
//

#define QEMU_CLEAR_TRACES() __asm__ __volatile__ ("out dx, al" : : "a"(0), "d"(0x402))

static inline void qemu_debug_string(const char *s, uint16_t len) {
  asm volatile (
    "rep outsb"
    : "+S"(s), "+c"(len)
    : "d"(0xe9)
    : "memory"
  );
}

static inline size_t __strlen(const char *s) {
  size_t len = 0;
  while (*s++) len++;
  return len;
}

#define QEMU_DEBUG_CHARP(str) qemu_debug_string(str, __strlen(str))

#endif
