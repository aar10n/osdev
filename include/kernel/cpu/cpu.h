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

#define CPU_BIT_FXSR          DEFINE_CPU_FEATURE(1, 0)
#define CPU_BIT_XSAVE         DEFINE_CPU_FEATURE(1, 1)
#define CPU_BIT_POPCNT        DEFINE_CPU_FEATURE(1, 2)
#define CPU_BIT_RDPID         DEFINE_CPU_FEATURE(1, 3)
#define CPU_BIT_RDTSCP        DEFINE_CPU_FEATURE(1, 4)
#define CPU_BIT_SYSCALL       DEFINE_CPU_FEATURE(1, 5)

#define CPU_BIT_NX            DEFINE_CPU_FEATURE(2, 0)
#define CPU_BIT_PGE           DEFINE_CPU_FEATURE(2, 1)
#define CPU_BIT_PDPE1GB       DEFINE_CPU_FEATURE(2, 2)
#define CPU_BIT_PML5          DEFINE_CPU_FEATURE(2, 3)

#define CPU_BIT_APIC          DEFINE_CPU_FEATURE(3, 0)
#define CPU_BIT_APIC_EXT      DEFINE_CPU_FEATURE(3, 1)
#define CPU_BIT_X2APIC        DEFINE_CPU_FEATURE(3, 2)
#define CPU_BIT_TSC           DEFINE_CPU_FEATURE(3, 3)
#define CPU_BIT_TSC_DEADLINE  DEFINE_CPU_FEATURE(3, 4)
#define CPU_BIT_TSC_INVARIANT DEFINE_CPU_FEATURE(3, 5)
#define CPU_BIT_TSC_ADJUST    DEFINE_CPU_FEATURE(3, 6)
#define CPU_BIT_WDT           DEFINE_CPU_FEATURE(3, 7)

#define CPU_BIT_UMIP          DEFINE_CPU_FEATURE(4, 0)

#define CPU_BIT_HTT           DEFINE_CPU_FEATURE(5, 0)
#define CPU_BIT_HYPERVISOR    DEFINE_CPU_FEATURE(5, 1)

typedef struct cpu_info {
  uint8_t cpu_id;
  uint8_t apic_id;
} cpu_info_t;

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
    uint16_t              : 3;
    // instruction support
    uint16_t fxsr         : 1;
    uint16_t xsave        : 1;
    uint16_t popcnt       : 1;
    uint16_t rdpid        : 1;
    uint16_t rdtscp       : 1;
    uint16_t syscall      : 1;
    uint16_t              : 10;
    // memory features
    uint16_t nx           : 1;
    uint16_t pge          : 1;
    uint16_t pdpe1gb      : 1;
    uint16_t pml5         : 1;
    uint16_t              : 12;
    // on-chip devices
    uint16_t apic         : 1;
    uint16_t apic_ext     : 1;
    uint16_t x2apic       : 1;
    uint16_t tsc          : 1;
    uint16_t tsc_deadline : 1;
    uint16_t tsc_inv      : 1;
    uint16_t tsc_adjust   : 1;
    uint16_t watchdog     : 1;
    uint16_t              : 8;
    // security features
    uint16_t umip         : 1;
    uint16_t              : 15;
    // other features
    uint16_t htt          : 1;
    uint16_t hypervisor   : 1;
    uint16_t              : 14;
  };
  uint16_t bits[6];
} cpu_features_t;
static_assert(sizeof(cpu_features_t) == 12);

typedef struct cpu_registers {

} cpu_registers_t;


void cpu_init();

uint32_t cpu_get_id();
int cpu_get_is_bsp();

int cpu_query_feature(uint64_t bit);

void cpu_print_info();
void cpu_print_features();

void cpu_flush_tlb();
void cpu_disable_interrupts();
void cpu_enable_interrupts();

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

uint64_t read_cr0();
void write_cr0(uint64_t cr0);
uint64_t __read_cr3();
void __write_cr3(uint64_t cr3);
uint64_t __read_cr4();
void __write_cr4(uint64_t cr4);

uint64_t __xgetbv(uint32_t index);
void __xsetbv(uint32_t index, uint64_t value);


void tlb_invlpg(uint64_t addr);
void tlb_flush();

void enable_sse();

int syscall(int call);
noreturn void sysret(uintptr_t rip, uintptr_t rsp);

#endif
