//
// Created by Aaron Gill-Braun on 2023-12-30.
//

#ifndef INCLUDE_KERNEL_BITS_H
#define INCLUDE_KERNEL_BITS_H

int __bsf32(uint32_t dword);
int __bsf64(uint64_t qword);
int __popcnt64(uint64_t qword);

#define bit_ffs32(x) __bsf32((uint32_t)(x))
#define bit_ffs64(x) __bsf64((uint64_t)(x))
#define bit_popcnt64(x) __popcnt64((uint64_t)(x))

#endif
