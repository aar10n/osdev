//
// Created by Aaron Gill-Braun on 2020-08-25.
//

#ifndef INCLUDE_KERNEL_CPU_CPU_H
#define INCLUDE_KERNEL_CPU_CPU_H

#include <stdint.h>

typedef struct __attribute__((packed)) {
  // general registers
  uint32_t eax;
  uint32_t ebx;
  uint32_t ecx;
  uint32_t edx;
  uint32_t esi;
  uint32_t edi;
  uint32_t esp;
  uint32_t ebp;
  // control registers
  uint32_t cr0;
  uint32_t cr2;
  uint32_t cr3;
  uint32_t cr4;
} cpu_t;

typedef struct {
  uint32_t ds;                                     // Data segment selector
  uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; // Pushed by pusha
  uint32_t int_no, err_code;                       // Interrupt number and error code
  uint32_t eip, cs, eflags, useresp, ss;           // Pushed by the processor automatically
} registers_t;

#endif
