//
// Created by Aaron Gill-Braun on 2020-08-25.
//

#ifndef KERNEL_CPU_CPU_H
#define KERNEL_CPU_CPU_H

#include <base.h>

#define DEFINE_CPU_FEATURE(word, bit) (((uint64_t)((word) & 0xFFFF) << 32) & (1 << (bit)))

#define IA32_APIC_BASE_MSR 0x1B
#define IA32_EFER_MSR    0xC0000080
#define IA32_TSC_MSR     0x10
#define IA32_TSC_AUX_MSR 0xC0000103
#define IA32_FS_BASE_MSR        0xC0000100
#define IA32_GS_BASE_MSR        0xC0000101
#define IA32_KERNEL_GS_BASE_MSR 0xC0000102

#define CPU_EXCEPTION_DE  0   // divide-by-zero error
#define CPU_EXCEPTION_DB  1   // debug
#define CPU_EXCEPTION_NMI 2   // non-maskable interrupt
#define CPU_EXCEPTION_BP  3   // breakpoint
#define CPU_EXCEPTION_OF  4   // overflow
#define CPU_EXCEPTION_BR  5   // bound range
#define CPU_EXCEPTION_UD  6   // invalid opcode
#define CPU_EXCEPTION_NM  7   // device not available (x87)
#define CPU_EXCEPTION_DF  8   // double fault
#define CPU_EXCEPTION_TS  10  // invalid tss
#define CPU_EXCEPTION_NP  11  // segment not present
#define CPU_EXCEPTION_SS  12  // stack
#define CPU_EXCEPTION_GP  13  // general protection fault
#define CPU_EXCEPTION_PF  14  // page fault
#define CPU_EXCEPTION_MF  16  // x87 floating-point exception pending
#define CPU_EXCEPTION_AC  17  // alignment check
#define CPU_EXCEPTION_MC  18  // machine check
#define CPU_EXCEPTION_XF  19  // simd floating point
#define CPU_EXCEPTION_CP  21  // control protection exception
#define CPU_EXCEPTION_HV  28  // hypervisor injection exception
#define CPU_EXCEPTION_VC  29  // vmm communication exception
#define CPU_EXCEPTION_SX  30  // security exception
#define CPU_MAX_EXCEPTION 31

#define CPU_BIT_MMX           DEFINE_CPU_FEATURE(0, 0)
#define CPU_BIT_SSE           DEFINE_CPU_FEATURE(0, 1)
#define CPU_BIT_SSE2          DEFINE_CPU_FEATURE(0, 2)
#define CPU_BIT_SSE3          DEFINE_CPU_FEATURE(0, 3)
#define CPU_BIT_SSSE3         DEFINE_CPU_FEATURE(0, 4)
#define CPU_BIT_SSE4_1        DEFINE_CPU_FEATURE(0, 5)
#define CPU_BIT_SSE4_2        DEFINE_CPU_FEATURE(0, 6)
#define CPU_BIT_SSE4A         DEFINE_CPU_FEATURE(0, 7)
#define CPU_BIT_AVX           DEFINE_CPU_FEATURE(0, 8)
#define CPU_BIT_AVX2          DEFINE_CPU_FEATURE(0, 9)
#define CPU_BIT_BMI1          DEFINE_CPU_FEATURE(0, 10)
#define CPU_BIT_BMI2          DEFINE_CPU_FEATURE(0, 11)
#define CPU_BIT_TBM           DEFINE_CPU_FEATURE(0, 12)
#define CPU_BIT_HLE           DEFINE_CPU_FEATURE(0, 13)

#define CPU_BIT_FXSR          DEFINE_CPU_FEATURE(1, 0)
#define CPU_BIT_XSAVE         DEFINE_CPU_FEATURE(1, 1)
#define CPU_BIT_POPCNT        DEFINE_CPU_FEATURE(1, 2)
#define CPU_BIT_RDPID         DEFINE_CPU_FEATURE(1, 3)
#define CPU_BIT_RDTSCP        DEFINE_CPU_FEATURE(1, 4)
#define CPU_BIT_SYSCALL       DEFINE_CPU_FEATURE(1, 5)
#define CPU_BIT_WAITPKG       DEFINE_CPU_FEATURE(1, 6)

#define CPU_BIT_NX            DEFINE_CPU_FEATURE(2, 0)
#define CPU_BIT_PGE           DEFINE_CPU_FEATURE(2, 1)
#define CPU_BIT_PAT           DEFINE_CPU_FEATURE(2, 2)
#define CPU_BIT_MTRR          DEFINE_CPU_FEATURE(2, 3)
#define CPU_BIT_PDPE1GB       DEFINE_CPU_FEATURE(2, 4)
#define CPU_BIT_PML5          DEFINE_CPU_FEATURE(2, 5)
#define CPU_BIT_TOPOEXT       DEFINE_CPU_FEATURE(2, 6)

#define CPU_BIT_APIC          DEFINE_CPU_FEATURE(3, 0)
#define CPU_BIT_APIC_EXT      DEFINE_CPU_FEATURE(3, 1)
#define CPU_BIT_X2APIC        DEFINE_CPU_FEATURE(3, 2)
#define CPU_BIT_ARAT          DEFINE_CPU_FEATURE(3, 3)
#define CPU_BIT_TSC           DEFINE_CPU_FEATURE(3, 4)
#define CPU_BIT_TSC_DEADLINE  DEFINE_CPU_FEATURE(3, 5)
#define CPU_BIT_TSC_INVARIANT DEFINE_CPU_FEATURE(3, 6)
#define CPU_BIT_TSC_ADJUST    DEFINE_CPU_FEATURE(3, 7)
#define CPU_BIT_WDT           DEFINE_CPU_FEATURE(3, 8)

#define CPU_BIT_UMIP          DEFINE_CPU_FEATURE(4, 0)

#define CPU_BIT_HYPERVISOR    DEFINE_CPU_FEATURE(5, 0)
#define CPU_BIT_HTT           DEFINE_CPU_FEATURE(5, 1)
#define CPU_BIT_DE            DEFINE_CPU_FEATURE(5, 2)
#define CPU_BIT_DS            DEFINE_CPU_FEATURE(5, 3)
#define CPU_BIT_DS_CPL        DEFINE_CPU_FEATURE(5, 4)
#define CPU_BIT_DTES64        DEFINE_CPU_FEATURE(5, 5)
#define CPU_BIT_DBX           DEFINE_CPU_FEATURE(5, 6)

typedef union cpu_features {
  struct {
    // instruction set support
    uint16_t mmx          : 1;
    uint16_t sse          : 1;
    uint16_t sse2         : 1;
    uint16_t sse3         : 1;
    uint16_t ssse3        : 1;
    uint16_t sse4_1       : 1;
    uint16_t sse4_2       : 1;
    uint16_t sse4a        : 1;
    uint16_t avx          : 1;
    uint16_t avx2         : 1;
    uint16_t bmi1         : 1;
    uint16_t bmi2         : 1;
    uint16_t tbm          : 1;
    uint16_t hle          : 1;
    uint16_t              : 2;
    // instruction support
    uint16_t fxsr         : 1;
    uint16_t xsave        : 1;
    uint16_t popcnt       : 1;
    uint16_t rdpid        : 1;
    uint16_t rdtscp       : 1;
    uint16_t syscall      : 1;
    uint16_t monitor      : 1;
    uint16_t waitpkg      : 1;
    uint16_t              : 8;
    // other features
    uint16_t nx           : 1;
    uint16_t pge          : 1;
    uint16_t pat          : 1;
    uint16_t mtrr         : 1;
    uint16_t pdpe1gb      : 1;
    uint16_t pml5         : 1;
    uint16_t topoext	    : 1;
    uint16_t              : 9;
    // on-chip devices
    uint16_t apic         : 1;
    uint16_t apic_ext     : 1;
    uint16_t x2apic       : 1;
    uint16_t arat         : 1;
    uint16_t tsc          : 1;
    uint16_t tsc_deadline : 1;
    uint16_t tsc_inv      : 1;
    uint16_t tsc_adjust   : 1;
    uint16_t watchdog     : 1;
    uint16_t              : 7;
    // security features
    uint16_t umip         : 1;
    uint16_t              : 15;
    // other features
    uint16_t hypervisor   : 1;
    uint16_t htt          : 1;
    uint16_t de           : 1;
    uint16_t ds           : 1;
    uint16_t ds_cpl       : 1;
    uint16_t dtes64       : 1;
    uint16_t dbx          : 1;
    uint16_t              : 9;
  };
  uint16_t bits[6];
} cpu_features_t;
static_assert(sizeof(cpu_features_t) == 12);

typedef struct cpu_info {
  uint8_t cpu_id;
  uint8_t apic_id;
  cpu_features_t features;
} cpu_info_t;

typedef struct cpu_registers {
  uint64_t rax, rbx, rcx, rdx;
  uint64_t rdi, rsi, rbp;
  uint64_t r8, r9, r10, r11;
  uint64_t r12, r13, r14, r15;
} cpu_registers_t;

typedef struct cpu_irq_stack {
  uint64_t rip;
  uint64_t cs;
  uint64_t rflags;
  uint64_t rsp;
  uint64_t ss;
} cpu_irq_stack_t;


void cpu_init();
void cpu_map_topology();

uint32_t cpu_get_id();
int cpu_get_is_bsp();

int cpu_query_feature(uint64_t bit);

void cpu_print_info();
void cpu_print_features();

void cpu_flush_tlb();
void cpu_disable_interrupts();
void cpu_enable_interrupts();

void cpu_disable_write_protection();
void cpu_enable_write_protection();

uint64_t cpu_read_stack_pointer();
void cpu_write_stack_pointer(uint64_t sp);

void cpu_load_gdt(void *gdt);
void cpu_load_idt(void *idt);
void cpu_load_tr(uint16_t tr);
void cpu_reload_segments();

// assembly functions

void cli();
void sti();

uint64_t cli_save();
void sti_restore(uint64_t rflags);

uint64_t __read_tsc();
uint64_t __read_tscp();

uint64_t read_tsc();

uint64_t __read_msr(uint32_t msr);
void __write_msr(uint32_t msr, uint64_t value);

uint64_t read_msr(uint32_t msr);
void write_msr(uint32_t msr, uint64_t value);

uint64_t read_fsbase();
void write_fsbase(uint64_t value);
uint64_t read_gsbase();
void write_gsbase(uint64_t value);
uint64_t read_kernel_gsbase();
void write_kernel_gsbase(uint64_t value);
void swapgs();

void load_gdt(void *gdtr);
void load_idt(void *idtr);
void load_tr(uint16_t tss);
void flush_gdt();

uint64_t __read_cr0();
void __write_cr0(uint64_t cr0);
uint64_t __read_cr2();
uint64_t __read_cr3();
void __write_cr3(uint64_t cr3);
uint64_t __read_cr4();
void __write_cr4(uint64_t cr4);

uint64_t __xgetbv(uint32_t index);
void __xsetbv(uint32_t index, uint64_t value);

int syscall(int call);
noreturn void sysret(uintptr_t rip, uintptr_t rsp);

#endif
