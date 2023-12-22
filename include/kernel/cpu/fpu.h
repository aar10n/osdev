//
// Created by Aaron Gill-Braun on 2023-12-10.
//

#include <stdint.h>

#ifndef KERNEL_CPU_FPU_H
#define KERNEL_CPU_FPU_H

// fxsave area
struct fpu_area {
  uint16_t fcw;            // control word
  uint16_t fsw;            // status word
  uint8_t ftw;             // tag word
  uint8_t : 8;             // reserved
  uint16_t fop;            // last instruction opcode
  uint64_t fip;            // instruction pointer
  uint64_t fdp;            // data pointer
  uint32_t mxcsr;          // mxcsr register state
  uint32_t mxcsr_mask;     // mxcsr mask
  struct {
    uint8_t	bytes[10];
    uint8_t rsvd[6];
  } st[8];                 // 8 80-bit fpu register
  uint8_t xmm[16][16];     // 16 128-bit xmm registers
  uint8_t pad[96];
} __aligned(16);
static_assert(sizeof(struct fpu_area) == 512);

struct fpu_area *fpu_area_alloc();
void fpu_area_free(struct fpu_area **fpa);

void fpu_save(struct fpu_area *fpa) alias("__fxsave");
void fpu_restore(struct fpu_area *fpa) alias("__fxrstor");

#endif
