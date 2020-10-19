//
// Created by Aaron Gill-Braun on 2020-10-17.
//

#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H

#include <base.h>

#define PROC_STACK_SIZE PAGE_SIZE

typedef struct {
  // pushed by us
  uint64_t rax;
  uint64_t rcx;
  uint64_t rdx;
  uint64_t rdi;
  uint64_t rsi;
  uint64_t r8;
  uint64_t r9;
  uint64_t r10;
  uint64_t r11;
  // pushed by cpu
  uint64_t rip;
  uint64_t cs;
  uint64_t rflags;
  uint64_t rsp;
  uint64_t ss;
} irq_context_t;

typedef struct {
  uint64_t rbx;
  uint64_t r12;
  uint64_t r13;
  uint64_t r14;
  uint64_t r15;
  uint64_t rbp;
  uint64_t rsp;
} proc_context_t;


typedef struct process {
  uint64_t pid;
  proc_context_t ctx;
  clock_t runtime;
  clock_t idletime;
} process_t;


#endif
