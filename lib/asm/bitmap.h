//
// Created by Aaron Gill-Braun on 2020-10-03.
//

#ifndef LIB_ASM_BITMAP_H
#define LIB_ASM_BITMAP_H

#include <stdint.h>

uint8_t __bt8(uint8_t byte, uint8_t bit);
uint8_t __bt64(uint64_t qword, uint8_t bit);

uint8_t __bts8(uint8_t *byte, uint8_t bit);
uint8_t __bts64(uint64_t *qword, uint8_t bit);

uint8_t __btr8(uint8_t *byte, uint8_t bit);
uint8_t __btr64(uint64_t *qword, uint8_t bit);

uint8_t __bsf8(uint8_t byte);
uint8_t __bsf64(uint64_t qword);

uint8_t __bsr8(uint8_t byte);
uint8_t __bsr64(uint64_t qword);

uint8_t __popcnt8(uint8_t byte);
uint8_t __popcnt64(uint64_t qword);

#endif
