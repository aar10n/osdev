//
// Created by Aaron Gill-Braun on 2020-10-17.
//

#ifndef KERNEL_PROCESS_H
#define KERNEL_PROCESS_H

#include <base.h>

#define DEFAULT_RFLAGS 0x246
#define PROC_STACK_SIZE PAGE_SIZE

typedef enum {
  READY,
  RUNNING,
  SLEEPING,
} proc_state_t;

typedef struct {
  // standard irq saved
  uint64_t rax;    // 0x00
  uint64_t rbx;    // 0x08
  uint64_t rcx;    // 0x10
  uint64_t rdx;    // 0x18
  uint64_t rdi;    // 0x20
  uint64_t rsi;    // 0x28
  uint64_t rbp;    // 0x30
  uint64_t r8;     // 0x38
  uint64_t r9;     // 0x40
  uint64_t r10;    // 0x48
  uint64_t r11;    // 0x50
  uint64_t r12;    // 0x58
  uint64_t r13;    // 0x60
  uint64_t r14;    // 0x68
  uint64_t r15;    // 0x70
  // pushed by cpu
  uint64_t rip;    // 0x78
  uint64_t cs;     // 0x80
  uint64_t rflags; // 0x88
  uint64_t rsp;    // 0x90
  uint64_t ss;     // 0x98
} context_t;

typedef struct process {
  uint64_t pid;
  context_t *ctx;
  uint8_t cpu;
  uint8_t policy;
  uint8_t priority;
  clock_t runtime;
  proc_state_t state;
  struct process *next;
} process_t;

void kthread_create(void (*func)());
void print_debug_process(process_t *process);

#endif
