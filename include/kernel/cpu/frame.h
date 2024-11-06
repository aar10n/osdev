//
// Created by Aaron Gill-Braun on 2024-01-02.
//

#ifndef KERNEL_CPU_FRAME_H
#define KERNEL_CPU_FRAME_H

struct trapframe {
  /* pushed by common handler */
  uint64_t rdi;
  uint64_t rsi;
  uint64_t rdx;
  uint64_t rcx;
  uint64_t r8;
  uint64_t r9;
  uint64_t rax;
  uint64_t rbx;
  uint64_t rbp;
  uint64_t r10;
  uint64_t r11;
  uint64_t r12;
  uint64_t r13;
  uint64_t r14;
  uint64_t r15;
  uint16_t	fs;
  uint16_t	gs;
  uint16_t	es;
  uint16_t	ds;
  /* pushed by stub */
  uint64_t data;
  uint64_t vector;
  /* pushed by processor */
  uint64_t error;
  uint64_t rip;
  uint64_t cs;
  uint64_t rflags;
  uint64_t rsp;
  uint64_t ss;
};
static_assert(sizeof(struct trapframe) == 0xc0); // referenced in exception.asm, switch.asm and syscall.asm

#endif
