//
// Created by Aaron Gill-Braun on 2020-10-20.
//

#ifndef LIB_ASM_ATOMIC_H
#define LIB_ASM_ATOMIC_H

#include <base.h>

uint64_t __atomic_fetch_add64(uint64_t *ptr, uint64_t value);
uint32_t __atomic_fetch_add32(uint32_t *ptr, uint32_t value);
uint16_t __atomic_fetch_add16(uint16_t *ptr, uint16_t value);
uint8_t __atomic_fetch_add8(uint8_t *ptr, uint8_t value);

uint8_t __atomic_bit_test_and_set(uint8_t *ptr, uint8_t b);
uint8_t __atomic_bit_test_and_reset(uint8_t *ptr, uint8_t b);

uint64_t __atomic_cmpxchg64(uint64_t *ptr, uint64_t value);

#endif
