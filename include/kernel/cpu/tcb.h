//
// Created by Aaron Gill-Braun on 2023-12-10.
//

#ifndef KERNEL_CPU_TCB_H
#define KERNEL_CPU_TCB_H

#include <stdint.h>
#include <kernel/cpu/fpu.h>

struct tcb {
  uint64_t rip;
  uint64_t rsp;
  uint64_t rbp;
  uint64_t rbx;
  uint64_t r12;
  uint64_t r13;
  uint64_t r14;
  uint64_t r15;
  uint64_t fsbase;
  uint64_t kgsbase;
  uint64_t dr0;
  uint64_t dr1;
  uint64_t dr2;
  uint64_t dr3;
  uint64_t dr6;
  uint64_t dr7;
  struct fpu_area *fpu;
  int tcb_flags;
};

#define TCB_KERNEL  0x01 // kernel thread context
#define TCB_FPU     0x02 // save fpu registers
#define TCB_DEBUG   0x04 // save debug registers
#define TCB_IRETQ   0x08 // needs full iretq

struct tcb *tcb_alloc(int flags);
void tcb_free(struct tcb **ptcb);

#endif
