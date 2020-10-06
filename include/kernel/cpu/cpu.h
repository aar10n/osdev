//
// Created by Aaron Gill-Braun on 2020-08-25.
//

#ifndef KERNEL_CPU_CPU_H
#define KERNEL_CPU_CPU_H

#include <stdint.h>
#include <base.h>

typedef struct packed {
  uint16_t apic_id;
  uint16_t vec_no;
  uint16_t err_code;
} cpu_err_t;

typedef struct packed {
  uint16_t apic_id;
  uint16_t vec_no;
  uint16_t int_no;
} cpu_int_t;

typedef struct packed {
  uint64_t rip, rflags;
  uint16_t cs, ds, es, fs, gs, ss;
  // general registers
  uint64_t rax, rbx, rcx, rdx;
  uint64_t rdi, rsi, rsp, rbp;
  // extended registers
  uint64_t r8, r9, r10, r11;
  uint64_t r12, r13, r14, r15;
  // control registers
  uint64_t cr0, cr2, cr3, cr4;
  // debug registers
  uint64_t dr0, dr1, dr2, dr3;
  uint64_t dr6, dr7;
} registers_t;

// CPUID Information

// cpuid: eax = 1
typedef struct {
  union {
    uint32_t raw;
    struct {
      uint32_t stepping_id : 4;
      uint32_t model : 4;
      uint32_t family_id : 4;
      uint32_t cpu_type : 2;
      uint32_t reserved0 : 2;
      uint32_t ext_model_id : 4;
      uint32_t ext_family_id : 8;
      uint32_t reserved1 : 4;
    };
  } eax;
  union {
    uint32_t raw;
    struct {
      uint8_t brand_index;
      uint8_t cache_line_size;
      uint8_t reserved;
      uint8_t local_apic_id;
    };
  } ebx;
  union {
    uint32_t raw;
    struct {
      uint32_t sse3 : 1;         // sse3
      uint32_t pclmulqdq : 1;    // pclmulqdq instruction
      uint32_t dtes64 : 1;       // 64-bit debug store
      uint32_t monitor : 1;      // monitor/mwait
      uint32_t ds_cpl : 1;       // cpl qualified debug store
      uint32_t vmx : 1;          // virtual machine extensions
      uint32_t smx : 1;          // safer mode extensions
      uint32_t est : 1;          // enhanced intel speed step
      uint32_t tm2 : 1;          // thermal monitor 2
      uint32_t ssse3 : 1;        // supplemental sse3 instructions
      uint32_t cntx_id : 1;      // L1 context id
      uint32_t sdbg : 1;         // silicon debug interface
      uint32_t fma : 1;          // fused multiply add
      uint32_t cx16 : 1;         // cmpxchg16b instruction
      uint32_t xtpr : 1;         // can disable sending task priority msgs
      uint32_t pdcm : 1;         // perform & debug capability
      uint32_t res0 : 1;         // reserved
      uint32_t pcid : 1;         // process context identifiers
      uint32_t dca : 1;          // direct cache access for DMA writes
      uint32_t sse41 : 1;        // sse4.1 instructions
      uint32_t sse42 : 1;        // sse4.2 instructions
      uint32_t x2apic : 1;       // x2APIC
      uint32_t movbe : 1;        // movbe instruction
      uint32_t popcnt : 1;       // popcnt instruction
      uint32_t tsc_deadline : 1; // apic implements one-shot operation using tsc deadline
      uint32_t aes : 1;          // aes instruction set
      uint32_t xsave : 1;        // xsave, xrestor, xsetbv, xgetbv instructions
      uint32_t osxsave : 1;      // xsave enabled by os
      uint32_t avx : 1;          // advanced vector extensions
      uint32_t f16c : 1;         // f16c (half-precision) fp feature
      uint32_t rdrnd : 1;        // rdrand (on-chip random number generator)
      uint32_t hypervisor : 1;   // hypervisor
    };
  } ecx;
  union {
    uint32_t raw;
    struct {
      uint32_t fpu : 1;   // floating point unit (x87)
      uint32_t vme : 1;   // virtual 8086 mode enhancements
      uint32_t de : 1;    // debugging extensions
      uint32_t pse : 1;   // page size extension
      uint32_t tsc : 1;   // time stamp counter
      uint32_t msr : 1;   // model specific register
      uint32_t pae : 1;   // page address extension
      uint32_t mce : 1;   // machine check exception
      uint32_t cx8 : 1;   // cmpxchg8b instruction - compare-and-exchange 8 bytes
      uint32_t apic : 1;  // apic on-chip
      uint32_t res0 : 1;  // reserved
      uint32_t sep : 1;   // sysenter and sysexit instructions
      uint32_t mtrr : 1;  // memory type range registers
      uint32_t pge : 1;   // pte global bit
      uint32_t mca : 1;   // machine check architecture
      uint32_t cmov : 1;  // conditional move instructions
      uint32_t pat : 1;   // page attribute table
      uint32_t pse36 : 1; // 36-bit page size extensions
      uint32_t psn : 1;   // processor serial number
      uint32_t clfsh : 1; // clflush instruction
      uint32_t res1 : 1;  // reserved
      uint32_t ds : 1;    // debug store
      uint32_t acpi : 1;  // thermal monitor and software controlled clock facilities
      uint32_t mmx : 1;   // intel mmx technology
      uint32_t fxsr : 1;  // fxsave and fxrstor instructions
      uint32_t sse : 1;   // sse
      uint32_t sse2 : 1;  // sse2
      uint32_t ss : 1;    // self snoop
      uint32_t htt : 1;   // hyper-threading technology
      uint32_t tm : 1;    // thermal monitor
      uint32_t ia64 : 1;  // IA64 processor emulation
      uint32_t pbe : 1;   // pending break enable
    };
  } edx;
} cpu_info_t;

// System Information

// logical core

typedef struct {
  uint8_t id;
  uint8_t version;
  uint8_t max_lvt;
  struct {
    uint8_t enabled : 1;
    uint8_t bsp : 1;
    uint8_t has_eoi_supress : 1;
    uint8_t reserved : 5;
  } flags;
} apic_desc_t;

typedef struct {
  uint8_t id;
  apic_desc_t *local_apic;
} core_desc_t;

typedef struct irq_source {
  uint8_t source_irq;
  uint8_t dest_interrupt;
  uint8_t flags;
  struct irq_source *next;
} irq_source_t;

typedef struct {
  uint8_t id;
  uint8_t version;
  uint8_t max_rentry;
  uint32_t address;
  uint32_t base;
  irq_source_t *sources;
} ioapic_desc_t;

typedef struct {
  uintptr_t apic_base;
  uint8_t bsp_id;
  uint8_t core_count;
  uint8_t ioapic_count;
  core_desc_t *cores;
  ioapic_desc_t *ioapics;
} system_info_t;

// Cpu Functions
void disable_interrupts();
void enable_interrupts();

void load_idt(void *idt);
void get_cpu_info(cpu_info_t *info);
void enable_sse();

#endif
