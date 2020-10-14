//
// Created by Aaron Gill-Braun on 2020-10-01.
//

#ifndef INCLUDE_BASE_H
#define INCLUDE_BASE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <boot.h>

#define noreturn _Noreturn
#define packed __attribute ((packed))
#define aligned(val) __attribute((aligned(val)))

#define static_assert(expr) _Static_assert(expr, "")

#define align(v, a) ((v) + (((a) - (v)) & ((a) - 1)))
#define align_ptr(p, a) ((void *) (align((uintptr_t)(p), (a))))

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))
#define abs(a) (((a) < 0) ? (-(a)) : (a))


#define LABEL(lbl) lbl: NULL

extern uintptr_t kernel_phys;
extern boot_info_t *boot_info;

#endif
