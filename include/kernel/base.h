//
// Created by Aaron Gill-Braun on 2020-10-01.
//

#ifndef INCLUDE_BASE_H
#define INCLUDE_BASE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

extern uintptr_t kernel_phys;

#define noreturn _Noreturn

// Helper Macros

#define align(v, a) ((v) + (((a) - (v)) & ((a) - 1)))
#define align_ptr(p, a) ((void *) (align((uintptr_t)(p), (a))))

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))
#define abs(a) (((a) < 0) ? (-(a)) : (a))

#endif
