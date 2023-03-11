//
// Created by Aaron Gill-Braun on 2022-06-07.
//

#include <cpu/cpu.h>
#include <cpu/gdt.h>
#include <cpu/idt.h>

#include <device/apic.h>
#include <device/pit.h>

#include <mm.h>
// #include <cpuid.h>
#include <string.h>
#include <panic.h>
#include <printf.h>

#define PERCPU_CPUID (PERCPU_CPU_INFO->cpuid_bits)

#define CPU_CR0_EM         (1 << 2)
#define CPU_CR0_WP         (1 << 16)
#define CPU_CR0_NW         (1 << 29)
#define CPU_CR0_CD         (1 << 30)

#define CPU_CR4_PGE        (1 << 7)
#define CPU_CR4_OSFXSR     (1 << 9)
#define CPU_CR4_OSXMMEXCPT (1 << 10)
#define CPU_CR4_UMIP       (1 << 11)
#define CPU_CR4_OSXSAVE    (1 << 18)

#define CPU_XCR0_X87       (1 << 0)
#define CPU_XCR0_SSE       (1 << 1)
#define CPU_XCR0_AVX       (1 << 2)
#define CPU_XCR0_OPMASK    (1 << 5) // AVX-512

#define CPU_EFER_SCE       (1 << 0)
#define CPU_EFER_NXE       (1 << 11)
#define CPU_EFER_FFXSR     (1 << 14)

uint8_t cpu_bsp_id = 0;
cpu_info_t cpu_bsp_info = {0};
per_cpu_t *percpu_structs[MAX_CPUS] = {};
uint8_t cpu_id_to_apic_id_table[MAX_CPUS] = {};


#define __cpuid(level, a, b, c, d) \
  __asm("cpuid\n\t" : "=a" (a), "=b" (b), "=c" (c), "=d" (d) : "0" (level))

static inline uint32_t __get_cpuid_max(uint32_t ext) {
  uint32_t eax, ebx, ecx, edx;
  __cpuid(ext, eax, ebx, ecx, edx);
  return eax;
}

static inline int __get_cpuid(uint32_t leaf,
                              uint32_t *eax, uint32_t *ebx, // NOLINT(readability-non-const-parameter)
                              uint32_t *ecx, uint32_t *edx) // NOLINT(readability-non-const-parameter)
{
  unsigned int ext = leaf & 0x80000000;
  unsigned int maxlevel = __get_cpuid_max(ext);
  if (maxlevel == 0 || maxlevel < leaf)
    return 0;

  __cpuid(leaf, *eax, *ebx, *ecx, *edx);
  return 1;
}

static inline int do_cpuid(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
  *a = 0;
  *b = 0;
  *c = 0;
  *d = 0;
  return __get_cpuid(leaf, a, b, c, d);
}

static inline void cpuid_clear_bit(uint16_t cpuid_bit) {
  uint8_t bit = cpuid_bit & 0xFF;
  uint8_t dword = (cpuid_bit >> 8) & 0xFF;
  if (bit > 31 || dword > (sizeof(cpuid_bits_t) / sizeof(uint32_t))) {
    return;
  }
  PERCPU_CPU_INFO->cpuid_bits.raw[dword] &= ~(uint32_t)(1 << bit);
}

static inline void assert_cpu_feature(const char *feature, int supported) {
  if (supported == 0) {
    panic("%s not supported by CPU", feature);
  }
}

static inline void bsp_log_message(const char *message) {
  if (PERCPU_ID != cpu_bsp_id) {
    return;
  }
  kprintf(message);
}

//

void cpu_init() {
  setup_gdt();
  setup_idt();
  apic_init();

  per_cpu_t *percpu_struct = (void *) cpu_read_gsbase();
  percpu_structs[PERCPU_ID] = percpu_struct;
  // num_percpu_structs++;

  uint32_t apic_id = cpu_get_apic_id();
  kassert(apic_id <= UINT8_MAX);

  cpuid_bits_t *cpuid_bits = NULL;
  if (PERCPU_IS_BSP) {
    cpuid_bits = &cpu_bsp_info.cpuid_bits;
    __percpu_struct_ptr()->apic_id = (uint8_t) apic_id;
    PERCPU_SET_CPU_INFO(&cpu_bsp_info);
  } else {
    cpu_info_t *cpu_info = kmalloc(sizeof(cpu_info_t));
    memset(cpu_info, 0, sizeof(cpu_info_t));
    cpuid_bits = &cpu_info->cpuid_bits;
    PERCPU_SET_CPU_INFO(cpu_info);
  }
  kassert(cpuid_bits != NULL);

  // load cpuid bits
  do_cpuid(0x00000001, &cpuid_bits->eax_0_1, &cpuid_bits->ebx_0_1, &cpuid_bits->ecx_0_1, &cpuid_bits->edx_0_1);
  do_cpuid(0x00000006, &cpuid_bits->eax_0_6, &cpuid_bits->ebx_0_6, &cpuid_bits->ecx_0_6, &cpuid_bits->edx_0_6);
  do_cpuid(0x00000007, &cpuid_bits->eax_0_7, &cpuid_bits->ebx_0_7, &cpuid_bits->ecx_0_7, &cpuid_bits->edx_0_7);
  do_cpuid(0x80000001, &cpuid_bits->eax_8_1, &cpuid_bits->ebx_8_1, &cpuid_bits->ecx_8_1, &cpuid_bits->edx_8_1);
  do_cpuid(0x80000007, &cpuid_bits->eax_8_7, &cpuid_bits->ebx_8_7, &cpuid_bits->ecx_8_7, &cpuid_bits->edx_8_7);
  do_cpuid(0x80000008, &cpuid_bits->eax_8_8, &cpuid_bits->ebx_8_8, &cpuid_bits->ecx_8_8, &cpuid_bits->edx_8_8);

  // if (PERCPU_IS_BSP) {
  //   cpu_print_info();
  //   cpu_print_cpuid();
  // }

  assert_cpu_feature("APIC", cpuid_query_bit(CPUID_BIT_APIC));
  assert_cpu_feature("TSC", cpuid_query_bit(CPUID_BIT_TSC));

  // SSE support
  assert_cpu_feature("CLFSH", cpuid_query_bit(CPUID_BIT_CLFSH));
  assert_cpu_feature("FXSR", cpuid_query_bit(CPUID_BIT_FXSR));
  assert_cpu_feature("MMX", cpuid_query_bit(CPUID_BIT_MMX));
  assert_cpu_feature("SSE", cpuid_query_bit(CPUID_BIT_SSE));
  assert_cpu_feature("SSE2", cpuid_query_bit(CPUID_BIT_SSE2));

  if (cpuid_query_bit(CPUID_BIT_HYBRID)) {
    panic("unsupported hybrid CPU topology detected");
  }

  // clear CR0.EM bit
  __write_cr0(__read_cr0() & ~CPU_CR0_EM);
  uint64_t cr4 = __read_cr4();
  cr4 |= CPU_CR4_OSFXSR | CPU_CR4_OSXMMEXCPT;

  // global page enable
  if (cpuid_query_bit(CPUID_BIT_PGE)) {
    bsp_log_message("PGE enabled\n");
    cr4 |= CPU_CR4_PGE;
  }
  // user mode instruction prevention
  if (cpuid_query_bit(CPUID_BIT_UMIP)) {
    bsp_log_message("UMIP enabled\n");
    cr4 |= CPU_CR4_UMIP;
  }
  // os enabled xsave
  if (cpuid_query_bit(CPUID_BIT_XSAVE) && cpuid_query_bit(CPUID_BIT_OSXSAVE)) {
    cr4 |= CPU_CR4_OSXSAVE;
  } else {
    bsp_log_message("XSAVE disabled\n");
    cpuid_clear_bit(CPUID_BIT_XSAVE);
  }
  __write_cr4(cr4);

  // enable XSAVE/XRSTOR Support
  if (cpuid_query_bit(CPUID_BIT_XSAVE)) {
    bsp_log_message("XSAVE support 'x87 registers'\n");
    bsp_log_message("XSAVE support 'SSE registers'\n");
    uint64_t xcr = __xgetbv(0x1);
    xcr |= CPU_XCR0_X87 | CPU_XCR0_SSE; // x87 state and SSE state
    // Enable AVX if available
    if (cpuid_query_bit(CPUID_BIT_AVX)) {
      bsp_log_message("XSAVE support 'AVX registers'\n");
      xcr |= CPU_XCR0_AVX; // AVX state
    }
    __xsetbv(0x1, xcr);
  }

  // enable NX and Fast FXSR
  uint64_t efer = cpu_read_msr(IA32_EFER_MSR);
  efer |= CPU_EFER_SCE;
  if (cpuid_query_bit(CPUID_BIT_NX)) {
    bsp_log_message("NX enabled\n");
    efer |= CPU_EFER_NXE;
  }
  if (cpuid_query_bit(CPUID_BIT_FXSR) && !cpuid_query_bit(CPUID_BIT_HYPERVISOR)) {
    bsp_log_message("FXSR enabled\n");
    efer |= CPU_EFER_FFXSR;
  }
  cpu_write_msr(IA32_EFER_MSR, efer);

  // save cpu id to aux msr
  cpu_write_msr(IA32_TSC_AUX_MSR, apic_id);

  cpu_id_to_apic_id_table[PERCPU_ID] = PERCPU_APIC_ID;
  if (PERCPU_IS_BSP) {
    cpu_print_info();
    cpu_print_cpuid();

    // callibrate processor frequency
    kprintf("calibrating processor frequency...\n");
    const uint64_t ms = 5;
    uint64_t cycles = UINT64_MAX;
    uint64_t t0, t1, dt;
    for (int i = 0; i < 5; i++) {
      t0 = cpu_read_tsc();
      apic_mdelay(ms);
      t1 = cpu_read_tsc();
      dt = t1 - t0;
      cycles = min(cycles, dt);
    }

    uint64_t cpu_ticks_per_sec = cycles * (MS_PER_SEC / ms);
    uint64_t cpu_clock_khz = cpu_ticks_per_sec / 1000;
    kprintf("detected %.2f MHz processor\n", (double)(cpu_clock_khz / 1000));
  }
}

void cpu_map_topology() {
  // cpuid leaf 0x8000001E
  uint32_t a, b, c, d;
  do_cpuid(0x8000001E, &a, &b, &c, &d);

  uint32_t apic_id = a;
  uint8_t compute_unit_id = b & 0xFF;
  uint8_t cores_per_compute_unit = ((b >> 8) & 0b11) + 1;
  uint8_t node_id = c & 0xFF;
  uint8_t nodes_per_processor = ((c >> 8) & 0b11) + 1;

  kprintf("processor topology:\n");
  kprintf("  apic id: %d\n", apic_id);
  kprintf("  compute unit id: %d\n", compute_unit_id);
  kprintf("  cores per compute unit: %d\n", cores_per_compute_unit);
  kprintf("  node id: %d\n", node_id);
  kprintf("  nodes per processor: %d\n", nodes_per_processor);
}

uint32_t cpu_get_apic_id() {
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
  uint32_t apic_base = cpu_read_msr(IA32_APIC_BASE_MSR);
  return (apic_base >> 8) & 1;
}

uint8_t cpu_id_to_apic_id(uint8_t cpu_id) {
  kassert(cpu_id < system_num_cpus);
  return cpu_id_to_apic_id_table[cpu_id];
}

int cpuid_query_bit(uint16_t feature) {
  uint8_t bit = feature & 0xFF;
  uint8_t dword = (feature >> 8) & 0xFF;
  if (bit > 31 || dword > (sizeof(cpuid_bits_t) / sizeof(uint32_t))) {
    return -1;
  }
  return (PERCPU_CPU_INFO->cpuid_bits.raw[dword] & (1 << bit)) != 0;
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

  uint8_t num_phys_bits = PERCPU_CPUID.eax_8_8 & 0xFF;
  uint8_t num_linear_bits = (PERCPU_CPUID.eax_8_8 >> 8) & 0xFF;
  uint8_t num_phys_cores = (PERCPU_CPUID.ecx_8_8 & 0xFF) + 1;
  uint8_t max_apic_id = 1 << ((PERCPU_CPUID.ecx_8_8 >> 12) & 0xF);

  kprintf("\n");
  kprintf("  Number of physical address bits: %d\n", num_phys_bits);
  kprintf("  Number of linear address bits: %d\n", num_linear_bits);
  kprintf("  Number of physical cores: %d\n", num_phys_cores);
  kprintf("  Max APIC ID: %d\n", max_apic_id);
}

void cpu_print_cpuid() {
  kprintf("CPUID:\n");
  kprintf("  apic: %d\n", cpuid_query_bit(CPUID_BIT_APIC));
  kprintf("  extapic: %d\n", cpuid_query_bit(CPUID_BIT_EXTAPIC));
  kprintf("  x2apic: %d\n", cpuid_query_bit(CPUID_BIT_X2APIC));
  kprintf("  tsc: %d\n", cpuid_query_bit(CPUID_BIT_TSC));
  kprintf("  tsc-deadline: %d\n", cpuid_query_bit(CPUID_BIT_TSC_DEADLINE));
  kprintf("  tsc-adjust: %d\n", cpuid_query_bit(CPUID_BIT_TSC_ADJUST));
  kprintf("  tsc-invariant: %d\n", cpuid_query_bit(CPUID_BIT_INVARIANT_TSC));
  kprintf("  perf-tsc: %d\n", cpuid_query_bit(CPUID_BIT_PERFTSC));
  kprintf("  arat: %d\n", cpuid_query_bit(CPUID_BIT_ARAT));
  kprintf("  wdt: %d\n", cpuid_query_bit(CPUID_BIT_WDT));
  kprintf("  topoext: %d\n", cpuid_query_bit(CPUID_BIT_TOPOEXT));
  kprintf("  htt: %d\n", cpuid_query_bit(CPUID_BIT_HTT));
  kprintf("\n");
  kprintf("  mmx: %d\n", cpuid_query_bit(CPUID_BIT_MMX));
  kprintf("  sse: %d\n", cpuid_query_bit(CPUID_BIT_SSE));
  kprintf("  sse2: %d\n", cpuid_query_bit(CPUID_BIT_SSE2));
  kprintf("  sse3: %d\n", cpuid_query_bit(CPUID_BIT_SSE3));
  kprintf("  sse4.1: %d\n", cpuid_query_bit(CPUID_BIT_SSE4_1));
  kprintf("  sse4.2: %d\n", cpuid_query_bit(CPUID_BIT_SSE4_2));
  kprintf("  avx: %d\n", cpuid_query_bit(CPUID_BIT_AVX));
  kprintf("  avx2: %d\n", cpuid_query_bit(CPUID_BIT_AVX2));
  kprintf("  avx512_f: %d\n", cpuid_query_bit(CPUID_BIT_AVX512_F));
  kprintf("\n");
  kprintf("  fxsr: %d\n", cpuid_query_bit(CPUID_BIT_FXSR));
  kprintf("  xsave: %d\n", cpuid_query_bit(CPUID_BIT_XSAVE));
  kprintf("  osxsave: %d\n", cpuid_query_bit(CPUID_BIT_OSXSAVE));
  kprintf("  pdpe1gb: %d\n", cpuid_query_bit(CPUID_BIT_PDPE1GB));
  kprintf("  nodeid_msr: %d\n", cpuid_query_bit(CPUID_BIT_PDPE1GB));
  kprintf("  mp: %d\n", cpuid_query_bit(CPUID_BIT_MP));
  kprintf("  nx: %d\n", cpuid_query_bit(CPUID_BIT_NX));
}

//

void cpu_disable_write_protection() {
  uint64_t cr0 = __read_cr0();
  __write_cr0(cr0 & ~CPU_CR0_WP);
}

void cpu_enable_write_protection() {
  uint64_t cr0 = __read_cr0();
  __write_cr0(cr0 | CPU_CR0_WP);
}
