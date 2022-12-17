//
// Created by Aaron Gill-Braun on 2020-10-01.
//

#ifndef INCLUDE_BASE_H
#define INCLUDE_BASE_H

#include <abi/types.h>
#include <boot.h>
#include <errno.h>

#define __PER_CPU_BASE__
#include <cpu/per_cpu.h>
#undef __PER_CPU_BASE__


//
// General Definitions
//

#define KERNEL_CS 0x08ULL
#define USER_DS   0x18ULL
#define USER_CS   0x20ULL

#define MS_PER_SEC 1000
#define US_PER_SEC 1000000
#define NS_PER_SEC 1000000000
#define FS_PER_SEC 1000000000000000

#define MS_TO_NS(ms) ((clock_t)(ms) * (NS_PER_SEC / MS_PER_SEC))
#define US_TO_NS(us) ((clock_t)(us) * (NS_PER_SEC / US_PER_SEC))
#define FS_TO_NS(fs) ((clock_t)(fs) / (FS_PER_SEC / NS_PER_SEC))
#define MS_TO_US(ms) ((clock_t)(ms) * (US_PER_SEC / MS_PER_SEC))

#define PAGE_SIZE 0x1000

#define SIZE_1KB  0x400
#define SIZE_2KB  0x800
#define SIZE_4KB  0x1000
#define SIZE_8KB  0x2000
#define SIZE_16KB 0x4000
#define SIZE_1MB  0x100000
#define SIZE_2MB  0x200000
#define SIZE_4MB  0x400000
#define SIZE_8MB  0x800000
#define SIZE_16MB 0x1000000
#define SIZE_1GB  0x40000000
#define SIZE_2GB  0x80000000
#define SIZE_4GB  0x100000000
#define SIZE_8GB  0x200000000
#define SIZE_16GB 0x400000000
#define SIZE_1TB  0x10000000000

//
// General Macros
//

#define static_assert(expr) _Static_assert(expr, "")

#define offset_ptr(p, c) ((void *)(((uintptr_t)(p)) + (c)))
#define offset_addr(p, c) (((uintptr_t)(p)) + (c))
#define align(v, a) ((v) + (((a) - (v)) & ((a) - 1)))
#define align_down(v, a) ((v) & ~((a) - 1))
#define is_aligned(v, a) (((v) & ((a) - 1)) == 0)
#define align_ptr(p, a) ((void *) (align((uintptr_t)(p), (a))))
#define ptr_after(s) ((void *)(((uintptr_t)(s)) + (sizeof(*s))))

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))
#define abs(a) (((a) < 0) ? (-(a)) : (a))
#define diff(a, b) abs((a) - (b))
#define udiff(a, b) (max(a, b) - min(a, b))

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

#define SIGNATURE_16(A, B) ((A) | ((B) << 8))
#define SIGNATURE_32(A, B, C, D) (SIGNATURE_16(A, B) | (SIGNATURE_16(C, D) << 16))
#define SIGNATURE_64(A, B, C, D, E, F, G, H) (SIGNATURE_32(A, B, C, D) | ((uint64_t) SIGNATURE_32(E, F, G, H) << 32))

#define ASSERT_IS_TYPE(type, value) static_assert(_Generic(value, type: 1, default: 0) == 1)

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
#define unused __attribute((unused))
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
 * was mapped to. Otherwise, it will be place in an unoccupied section of memory
 * and the struct will contain the physical address of where it was placed. It is
 * up to the kernel to later map these sections into virtual memory.
 */
#define LOAD_SECTION(varname, secname) loaded_section_t __attribute__((section(".load_sections"))) varname = { .name = secname }

/**
 * The MODULE_INIT macro provides a way for kernel components to register initializer
 * functions without changing any external code. The given function is called after
 * the core kernel initialization. This means that initializers are free to call most
 * standard APIs provided by the kernel but this does not include the scheduler or any
 * process/thread related functions.
 */
#define MODULE_INIT(fn) static void __attribute__((constructor)) __do_module_init_ ## fn () { fn(); }

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

#endif
