//
// Created by Aaron Gill-Braun on 2022-06-07.
//

#include <cpu/cpu.h>
#include <cpu/gdt.h>
#include <cpu/idt.h>

#include <device/pit.h>

#include <cpuid.h>
#include <panic.h>
#include <printf.h>

void __flush_tlb();
void __disable_interrupts();
void __enable_interrupts();

// 0x1
#define CPU_EDX_TSC        (1 << 4)
#define CPU_EDX_APIC       (1 << 9)
#define CPU_EDX_PGE        (1 << 13)
#define CPU_EDX_CLFSH      (1 << 19)
#define CPU_EDX_MMX        (1 << 23)
#define CPU_EDX_FXSR       (1 << 24)
#define CPU_EDX_SSE        (1 << 25)
#define CPU_EDX_SSE2       (1 << 26)
#define CPU_EDX_HTT        (1 << 28)

#define CPU_ECX_SSE3       (1 << 0)
#define CPU_ECX_SSSE3      (1 << 9)
#define CPU_ECX_SSE4_1     (1 << 19)
#define CPU_ECX_SSE4_2     (1 << 20)
#define CPU_ECX_X2APIC     (1 << 21)
#define CPU_ECX_POPCNT     (1 << 23)
#define CPU_ECX_TSCDEADL   (1 << 24)
#define CPU_ECX_XSAVE      (1 << 26)
#define CPU_ECX_OSXSAVE    (1 << 27)
#define CPU_ECX_AVX        (1 << 28)
#define CPU_ECX_HYPERVISOR (1 << 31)

// 0x7
#define CPU_EBX_TSC_ADJUST (1 << 1)
#define CPU_EBX_BMI1       (1 << 3)
#define CPU_EBX_AVX2       (1 << 5)
#define CPU_EBX_BMI2       (1 << 8)

#define CPU_ECX_UMIP       (1 << 2)
#define CPU_ECX_PML5       (1 << 16)
#define CPU_ECX_RDPID      (1 << 22)

#define CPU_EDX_HYBRID     (1 << 15)

// 0x80000001
#define CPU_EDX_SYSCALL    (1 << 11)
#define CPU_EDX_NX         (1 << 20)
#define CPU_EDX_PDPE1GB    (1 << 26)
#define CPU_EDX_RDTSCP     (1 << 27)

#define CPU_ECX_EXTAPIC    (1 << 3)
#define CPU_ECX_SSE4A      (1 << 6)
#define CPU_ECX_WDT        (1 << 13)
#define CPU_ECX_TBM        (1 << 21)

// 0x80000007
#define CPU_EDX_INVTSC     (1 << 8)

#define CPU_CR4_PGE        (1 << 7)
#define CPU_CR4_OSFXSR     (1 << 9)
#define CPU_CR4_OSXMMEXCPT (1 << 10)
#define CPU_CR4_UMIP       (1 << 11)
#define CPU_CR4_OSXSAVE    (1 << 18)

#define CPU_XCR0_X87       (1 << 0)
#define CPU_XCR0_SSE       (1 << 1)
#define CPU_XCR0_AVX       (1 << 2)
#define CPU_XCR0_OPMASK    (1 << 5) // AVX-512

#define CPU_EFER_NX        (1 << 11)
#define CPU_EFER_FFXSR     (1 << 14)

cpu_features_t features = {0};

static inline int do_cpuid(int leaf, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
  *a = 0;
  *b = 0;
  *c = 0;
  *d = 0;
  return __get_cpuid(leaf, a, b, c, d);
}

static inline void assert_cpu_feature(const char *feature, int supported) {
  if (supported == 0) {
    panic("%s not supported by CPU", feature);
  }
}

//

void cpu_init() {
  // setup_gdt();
  // setup_idt();

  // cpuid leaf 0x1
  uint32_t a0_1, b0_1, c0_1, d0_1;
  do_cpuid(0x1, &a0_1, &b0_1, &c0_1, &d0_1);
  // cpuid leaf 0x7
  uint32_t a0_7, b0_7, c0_7, d0_7;
  do_cpuid(0x7, &a0_7, &b0_7, &c0_7, &d0_7);
  // cpuid leaf 0x80000001
  uint32_t a8_1, b8_1, c8_1, d8_1;
  do_cpuid(0x80000001, &a8_1, &b8_1, &c8_1, &d8_1);
  // cpuid leaf 0x80000007
  uint32_t a8_7, b8_7, c8_7, d8_7;
  do_cpuid(0x80000007, &a8_7, &b8_7, &c8_7, &d8_7);

  cpu_print_info();
  // cpu_print_features();

  // Get CPU features
  features.mmx = (d0_1 & CPU_EDX_MMX) != 0;
  features.sse = (d0_1 & CPU_EDX_SSE) != 0;
  features.sse2 = (d0_1 & CPU_EDX_SSE2) != 0;
  features.sse3 = (c0_1 & CPU_ECX_SSE3) != 0;
  features.ssse3 = (c0_1 & CPU_ECX_SSSE3) != 0;
  features.sse4_1 = (c0_1 & CPU_ECX_SSE4_1) != 0;
  features.sse4_2 = (c0_1 & CPU_ECX_SSE4_2) != 0;
  features.sse4a = (c8_1 & CPU_ECX_SSE4A) != 0;
  features.avx = (c0_1 & CPU_ECX_AVX) != 0;
  features.avx2 = (b0_7 & CPU_EBX_AVX2) != 0;
  features.bmi1 = (b0_7 & CPU_EBX_BMI1) != 0;
  features.bmi2 = (b0_7 & CPU_EBX_BMI2) != 0;
  features.tbm = (c8_1 & CPU_ECX_TBM) != 0;

  features.fxsr = (d0_1 & CPU_EDX_FXSR) != 0;
  features.xsave = (c0_1 & CPU_ECX_XSAVE) != 0;
  features.popcnt = (c0_1 & CPU_ECX_POPCNT) != 0;
  features.rdpid = (c0_7 & CPU_ECX_RDPID) != 0;
  features.rdtscp = (d8_1 & CPU_EDX_RDTSCP) != 0;
  features.syscall = (d8_1 & CPU_EDX_SYSCALL) != 0;

  features.nx = (d8_1 & CPU_EDX_NX) != 0;
  features.pge = (d0_1 & CPU_EDX_PGE) != 0;
  features.pdpe1gb = (d8_1 & CPU_EDX_PDPE1GB) != 0;
  features.pml5 = (c0_7 & CPU_ECX_PML5) != 0;

  features.apic = (d0_1 & CPU_EDX_APIC) != 0;
  features.apic_ext = (c8_1 & CPU_ECX_EXTAPIC) != 0;
  features.x2apic = (c0_1 & CPU_ECX_X2APIC) != 0;
  features.tsc = (d0_1 & CPU_EDX_TSC) != 0;
  features.tsc_deadline = (d0_1 & CPU_ECX_TSCDEADL) != 0;
  features.tsc_inv = (d8_7 & CPU_EDX_INVTSC) != 0;
  features.tsc_adjust = (b0_7 & CPU_EBX_TSC_ADJUST) != 0;
  features.watchdog = (c8_7 & CPU_ECX_WDT) != 0;

  features.umip = (c0_7 & CPU_ECX_UMIP) != 0;

  features.htt = (d0_1 & CPU_EDX_HTT) != 0;
  features.hypervisor = (c0_1 & CPU_ECX_HYPERVISOR) != 0;

  if (d0_7 & CPU_EDX_HYBRID) {
    panic("detected unsupported hybrid CPU topology");
  }

  assert_cpu_feature("APIC", features.apic);
  assert_cpu_feature("TSC", features.tsc);

  // SSE support
  assert_cpu_feature("CLFSH", (d0_1 & CPU_EDX_CLFSH));
  assert_cpu_feature("FXSR", features.fxsr);
  assert_cpu_feature("MMX", features.mmx);
  assert_cpu_feature("SSE", features.sse);
  assert_cpu_feature("SSE2", features.sse2);

  uint64_t cr4 = __read_cr4();
  cr4 |= CPU_CR4_OSFXSR | CPU_CR4_OSXMMEXCPT;

  // global page enable
  if (features.pge) {
    kprintf("PGE enabled\n");
    cr4 |= CPU_CR4_PGE;
  }
  // user mode instruction prevention
  if (features.umip) {
    kprintf("UMIP enabled\n");
    cr4 |= CPU_CR4_UMIP;
  }
  // os enabled xsave
  if (features.xsave && (c0_1 & CPU_ECX_OSXSAVE)) {
    cr4 |= CPU_CR4_OSXSAVE;
  } else {
    kprintf("XSAVE disabled\n");
    features.xsave = false;
  }
  __write_cr4(cr4);

  // enable XSAVE/XRSTOR Support
  if (features.xsave) {
    kprintf("XSAVE support 'x87 registers'\n");
    kprintf("XSAVE support 'SSE registers'\n");
    uint64_t xcr = __xgetbv(0x1);
    xcr |= CPU_XCR0_X87 | CPU_XCR0_SSE; // x87 state and SSE state
    // Enable AVX if available
    if (features.avx) {
      kprintf("XSAVE support 'AVX registers'\n");
      xcr |= CPU_XCR0_AVX; // AVX state
    }
    __xsetbv(0x1, xcr);
  }

  // enable NX and Fast FXSR
  uint64_t efer = __read_msr(IA32_EFER_MSR);
  if (features.nx) {
    kprintf("NX enabled\n");
    efer |= CPU_EFER_NX;
  }
  if (features.fxsr && !features.hypervisor) {
    kprintf("FXSR enabled\n");
    efer |= CPU_EFER_FFXSR;
  }
  __write_msr(IA32_EFER_MSR, efer);


  // save cpu id to aux msr
  uint32_t id = cpu_get_id();
  __write_msr(IA32_TSC_AUX_MSR, id);

  // callibrate processor frequency
  kprintf("calibrating processor frequency...\n");

  const uint64_t ms = 5;
  uint64_t cycles = UINT64_MAX;
  uint64_t t0, t1, dt;
  for (int i = 0; i < 5; i++) {
    t0 = __read_tsc();
    pit_mdelay(ms);
    t1 = __read_tsc();
    dt = t1 - t0;
    cycles = min(cycles, dt);
  }

  uint64_t cpu_ticks_per_sec = cycles * (MS_PER_SEC / ms);
  uint64_t cpu_clock_khz = cpu_ticks_per_sec / 1000;
  kprintf("detected %.2f MHz processor\n", (double)(cpu_clock_khz / 1000));
}

uint32_t cpu_get_id() {
  uint32_t a, b, c, d;
  // try leaf 0x1F to get the x2APIC id
  if (__get_cpuid(0x1F, &a, &b, &c, &d)) {
    return d;
  }
  // then try leaf 0x0B to get the x2APIC id
  if (__get_cpuid(0x0B, &a, &b, &c, &d)) {
    return d;
  }
  // fall back to the initial APIC id
  __get_cpuid(0x1, &a, &b, &c, &d);
  return (b >> 24) & 0xFF;
}

int cpu_get_is_bsp() {
  uint32_t apic_base = read_msr(IA32_APIC_BASE_MSR);
  return (apic_base >> 8) & 1;
}

int cpu_query_feature(uint64_t bit) {
  uint32_t w = (bit >> 32) & UINT32_MAX;
  uint32_t m = bit & UINT32_MAX;
  if (w >= 6) {
    return 0;
  }
  return features.bits[w] & m;
}

void cpu_print_info() {
  char id_string[13];
  uint32_t max_leaf;
  do_cpuid(0x0, &max_leaf, (uint32_t *) &id_string[0], (uint32_t *) &id_string[8], (uint32_t *) &id_string[4]);
  id_string[12] = 0;

  uint32_t brand_string[12];
  do_cpuid(0x80000002, brand_string + 0x0, brand_string + 0x1, brand_string + 0x2, brand_string + 0x3);
  do_cpuid(0x80000003, brand_string + 0x4, brand_string + 0x5, brand_string + 0x6, brand_string + 0x7);
  do_cpuid(0x80000004, brand_string + 0x8, brand_string + 0x9, brand_string + 0xA, brand_string + 0xB);

  uint32_t a, b, c, d;
  do_cpuid(0x1, &a, &b, &c, &d);

  uint32_t stepping = (a >> 0) & 0xF;
  uint32_t model = ((a >> 4) & 0xF) | (((a >> 16) & 0xF) << 4);
  uint32_t family = ((a >> 8) & 0xF) | (((a >> 20) & 0xFF) << 4);
  uint32_t type = (a >> 12) & 0x3;

  const char *type_str;
  switch (type) {
    case 0: type_str = "Original OEM"; break;
    case 1: type_str = "Overdrive"; break;
    case 2: type_str = "Dual Core"; break;
    case 3: type_str = "Intel Reserved"; break;
    default: panic("unknown CPU type");
  }

  char *brand_string_ptr = (char *) brand_string;
  while (*brand_string_ptr == ' ') {
    brand_string_ptr++;
  }

  kprintf("Processor Info:\n");
  kprintf("  Vendor:     %s\n", id_string);
  kprintf("  Model:      %s\n", brand_string_ptr);
  kprintf("  Type:       %s\n", type_str);
  kprintf("  Family:     %-2d (%02xh)\n", family, family);
  kprintf("  Model:      %-2d (%02xh)\n", model, model);
  kprintf("  Stepping:   %-2d (%02xh)\n", stepping, stepping);
}

void cpu_print_features() {
  kprintf("CPU Features:\n");
  kprintf("    MMX: %d\n", features.mmx);
  kprintf("    SSE: %d\n", features.sse);
  kprintf("    SSE2: %d\n", features.sse2);
  kprintf("    SSE3: %d\n", features.sse3);
  kprintf("    SSSE3: %d\n", features.ssse3);
  kprintf("    SSE4.1: %d\n", features.sse4_1);
  kprintf("    SSE4.2: %d\n", features.sse4_2);
  kprintf("    SSE4A: %d\n", features.sse4a);
  kprintf("    AVX: %d\n", features.avx);
  kprintf("    AVX2: %d\n", features.avx2);
  kprintf("    BMI1: %d\n", features.bmi1);
  kprintf("    BMI2: %d\n", features.bmi2);
  kprintf("    TBM: %d\n", features.tbm);
  kprintf("\n");
  kprintf("    FXSR: %d\n", features.fxsr);
  kprintf("    XSAVE: %d\n", features.xsave);
  kprintf("    POPCNT: %d\n", features.popcnt);
  kprintf("    RDPID: %d\n", features.rdpid);
  kprintf("    RDTSCP: %d\n", features.rdtscp);
  kprintf("    SYSCALL: %d\n", features.syscall);
  kprintf("\n");
  kprintf("    NX: %d\n", features.nx);
  kprintf("    PGE: %d\n", features.pge);
  kprintf("    PDPE1GB: %d\n", features.pdpe1gb);
  kprintf("    PML5: %d\n", features.pml5);
  kprintf("\n");
  kprintf("    APIC: %d\n", features.apic);
  kprintf("    APIC_EXT: %d\n", features.apic_ext);
  kprintf("    X2APIC: %d\n", features.x2apic);
  kprintf("    TSC: %d\n", features.tsc);
  kprintf("    TSC_DEADLINE: %d\n", features.tsc_deadline);
  kprintf("    TSC_INV: %d\n", features.tsc_inv);
  kprintf("    TSC_ADJUST: %d\n", features.tsc_adjust);
  kprintf("    WATCHDOG: %d\n", features.watchdog);
  kprintf("\n");
  kprintf("    UMIP: %d\n", features.umip);
  kprintf("\n");
  kprintf("    HTT: %d\n", features.htt);
  kprintf("    HYPERVISOR: %d\n", features.hypervisor);
}

//

void cpu_flush_tlb() {
  __flush_tlb();
}

void cpu_disable_interrupts() {
  __disable_interrupts();
}

void cpu_enable_interrupts() {
  __enable_interrupts();
}
