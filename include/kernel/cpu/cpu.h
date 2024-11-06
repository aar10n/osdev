//
// Created by Aaron Gill-Braun on 2020-08-25.
//

#ifndef KERNEL_CPU_CPU_H
#define KERNEL_CPU_CPU_H

#include <kernel/base.h>

#define MAX_CPUS 64

#define NULL_SEG    0x00ULL
#define KCODE_SEG   0x08ULL
#define KDATA_SEG   0x10ULL
#define UCODE32_SEG 0x18ULL
#define UDATA_SEG   0x20ULL
#define UCODE64_SEG 0x28ULL
#define TSS_LO_SEG  0x30ULL
#define TSS_HI_SEG  0x38ULL

#define IA32_TSC_MSR            0x10
#define IA32_APIC_BASE_MSR      0x1B
#define IA32_EFER_MSR           0xC0000080
#define IA32_STAR_MSR           0xC0000081 // ring 0 and ring 3 segment bases (and syscall eip)
#define IA32_LSTAR_MSR          0xC0000082 // rip syscall entry for 64-bit software
#define IA32_CSTAR_MSR          0xC0000083 // rip syscall entry for compatibility mode
#define IA32_SFMASK_MSR         0xC0000084 // syscall flag mask
#define IA32_TSC_AUX_MSR        0xC0000103
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

#define CPU_PF_P  (1 << 0) // present (0 = not present, 1 = present)
#define CPU_PF_W  (1 << 1) // write (0 = read, 1 = write)
#define CPU_PF_U  (1 << 2) // user (0 = supervisor, 1 = user)
#define CPU_PF_I  (1 << 4) // instruction fetch (when NX is enabled)

typedef union cpuid_bits {
  struct {
    // leaf 0x00000001
    uint32_t eax_0_1, ebx_0_1, ecx_0_1, edx_0_1;
    // leaf 0x00000006
    uint32_t eax_0_6, ebx_0_6, ecx_0_6, edx_0_6;
    // leaf 0x00000007
    uint32_t eax_0_7, ebx_0_7, ecx_0_7, edx_0_7;
    // leaf 0x80000001
    uint32_t eax_8_1, ebx_8_1, ecx_8_1, edx_8_1;
    // leaf 0x80000007
    uint32_t eax_8_7, ebx_8_7, ecx_8_7, edx_8_7;
    // leaf 0x80000008
    uint32_t eax_8_8, ebx_8_8, ecx_8_8, edx_8_8;
  };
  uint32_t raw[24];
} cpuid_bits_t;
#define _CPUID_BIT(member, bit) (((offsetof(cpuid_bits_t, member) / sizeof(uint32_t)) << 8) | ((bit) & 0xFF))

#define CPUID_BIT_DE            _CPUID_BIT(edx_0_1, 2)
#define CPUID_BIT_TSC           _CPUID_BIT(edx_0_1, 4)
#define CPUID_BIT_APIC          _CPUID_BIT(edx_0_1, 8)
#define CPUID_BIT_PGE           _CPUID_BIT(edx_0_1, 13)
#define CPUID_BIT_PAT           _CPUID_BIT(edx_0_1, 16)
#define CPUID_BIT_CLFSH         _CPUID_BIT(edx_0_1, 19)
#define CPUID_BIT_DS            _CPUID_BIT(edx_0_1, 21)
#define CPUID_BIT_MMX           _CPUID_BIT(edx_0_1, 23)
#define CPUID_BIT_FXSR          _CPUID_BIT(edx_0_1, 24)
#define CPUID_BIT_SSE           _CPUID_BIT(edx_0_1, 25)
#define CPUID_BIT_SSE2          _CPUID_BIT(edx_0_1, 26)
#define CPUID_BIT_HTT           _CPUID_BIT(edx_0_1, 28)

#define CPUID_BIT_SSE3          _CPUID_BIT(ecx_0_1, 0)
#define CPUID_BIT_DTES64        _CPUID_BIT(ecx_0_1, 2)
#define CPUID_BIT_DS_CPL        _CPUID_BIT(ecx_0_1, 4)
#define CPUID_BIT_SSSE3         _CPUID_BIT(ecx_0_1, 9)
#define CPUID_BIT_SSE4_1        _CPUID_BIT(ecx_0_1, 19)
#define CPUID_BIT_SSE4_2        _CPUID_BIT(ecx_0_1, 20)
#define CPUID_BIT_X2APIC        _CPUID_BIT(ecx_0_1, 21)
#define CPUID_BIT_TSC_DEADLINE  _CPUID_BIT(ecx_0_1, 24)
#define CPUID_BIT_XSAVE         _CPUID_BIT(ecx_0_1, 26)
#define CPUID_BIT_OSXSAVE       _CPUID_BIT(ecx_0_1, 27)
#define CPUID_BIT_AVX           _CPUID_BIT(ecx_0_1, 28)
#define CPUID_BIT_HYPERVISOR    _CPUID_BIT(ecx_0_1, 31)

#define CPUID_BIT_ARAT          _CPUID_BIT(eax_0_6, 2)

#define CPUID_BIT_FSGSBASE      _CPUID_BIT(ebx_0_7, 0)
#define CPUID_BIT_TSC_ADJUST    _CPUID_BIT(ebx_0_7, 1)
#define CPUID_BIT_BMI1          _CPUID_BIT(ebx_0_7, 3)
#define CPUID_BIT_HLE           _CPUID_BIT(ebx_0_7, 4)
#define CPUID_BIT_AVX2          _CPUID_BIT(ebx_0_7, 5)
#define CPUID_BIT_SMEP          _CPUID_BIT(ebx_0_7, 7)
#define CPUID_BIT_BMI2          _CPUID_BIT(ebx_0_7, 8)
#define CPUID_BIT_AVX512_F      _CPUID_BIT(ebx_0_7, 16)

#define CPUID_BIT_UMIP          _CPUID_BIT(ecx_0_7, 2)
#define CPUID_BIT_WAITPKG       _CPUID_BIT(ecx_0_7, 5)
#define CPUID_BIT_PML5          _CPUID_BIT(ecx_0_7, 16)
#define CPUID_BIT_RDPID         _CPUID_BIT(ecx_0_7, 22)

#define CPUID_BIT_HYBRID        _CPUID_BIT(edx_0_7, 15)

#define CPUID_BIT_MP            _CPUID_BIT(edx_8_1, 19)
#define CPUID_BIT_NX            _CPUID_BIT(edx_8_1, 20)
#define CPUID_BIT_PDPE1GB       _CPUID_BIT(edx_8_1, 26)
#define CPUID_BIT_RDTSCP        _CPUID_BIT(edx_8_1, 27)

#define CPUID_BIT_SVM           _CPUID_BIT(ecx_8_1, 2)
#define CPUID_BIT_EXTAPIC       _CPUID_BIT(ecx_8_1, 3)
#define CPUID_BIT_SSE4A         _CPUID_BIT(ecx_8_1, 6)
#define CPUID_BIT_MISALIGNSSE   _CPUID_BIT(ecx_8_1, 7)
#define CPUID_BIT_WDT           _CPUID_BIT(ecx_8_1, 13)
#define CPUID_BIT_NODEID_MSR    _CPUID_BIT(ecx_8_1, 19)
#define CPUID_BIT_TOPOEXT       _CPUID_BIT(ecx_8_1, 22)
#define CPUID_BIT_PERFTSC       _CPUID_BIT(ecx_8_1, 27)

#define CPUID_BIT_INVARIANT_TSC _CPUID_BIT(edx_8_7, 8)

struct cpu_info {
  uint32_t apic_id;
  cpuid_bits_t cpuid_bits;
};

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

#define temp_irq_save(flags) ({ ASSERT_IS_TYPE(uint64_t, flags); (flags) = cpu_save_clear_interrupts(); (flags); })
#define temp_irq_restore(flags) ({ ASSERT_IS_TYPE(uint64_t, flags); cpu_restore_interrupts(flags); })

#define cpu_invlpg(addr) ({ uintptr_t __x = (uintptr_t)(addr); __asm volatile("invlpg [%0]" :: "r" (__x) : "memory"); })

extern uint8_t cpu_bsp_id;

void cpu_early_init();
void cpu_late_init();
void cpu_map_topology();

uint32_t cpu_get_apic_id();
int cpu_get_is_bsp();
uint8_t cpu_id_to_apic_id(uint8_t cpu_id);

int cpuid_query_bit(uint16_t feature);

void cpu_print_info();
void cpu_print_cpuid();

void cpu_disable_interrupts();
void cpu_enable_interrupts();
uint64_t cpu_save_clear_interrupts();
void cpu_restore_interrupts(uint64_t flags);

void cpu_disable_write_protection();
void cpu_enable_write_protection();

uint64_t cpu_read_stack_pointer();
void cpu_write_stack_pointer(uint64_t sp);

void cpu_load_gdt(void *gdt);
void cpu_load_idt(void *idt);
void cpu_load_tr(uint16_t tr);
void cpu_set_cs(uint16_t cs);
void cpu_set_ds(uint16_t ds);
void cpu_flush_tlb();

uint64_t cpu_read_msr(uint32_t msr);
uint64_t cpu_write_msr(uint32_t msr, uint64_t value);

uint64_t cpu_read_tsc();

uint64_t cpu_read_fsbase();
void cpu_write_fsbase(uint64_t value);
uint64_t cpu_read_gsbase();
void cpu_write_gsbase(uint64_t value);
uint64_t cpu_read_kernel_gsbase();
void cpu_write_kernel_gsbase(uint64_t value);

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
