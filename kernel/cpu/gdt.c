//
// Created by Aaron Gill-Braun on 2019-04-24.
//

#include <cpu/cpu.h>
#include <cpu/gdt.h>
#include <string.h>
#include <mm.h>

#define segment(base, limit, typ, s, dpl, p, is64, is32, g)  \
  ((gdt_entry_t) {                                            \
    .limit_low = (limit) & 0xFFFF, .base_low = (base) & 0xFFFF, \
    .base_mid = (base) >> 16, .type = typ, .desc_type = s, .cpl = dpl, \
    .present = p, .limit_high = (limit) >> 16, .available = 0, .long_desc = is64, \
    .op_size = is32, .granularity = g, .base_high = (base) >> 24 \
  })

#define system_segment_low(base, limit, typ, s, dpl, p, is64, is32, g) \
  ((gdt_entry_t) {                                                 \
    .limit_low = (limit) & 0xFFFF, .base_low = (base) & 0xFFFF, \
    .base_mid = (base) >> 16, .type = typ, .desc_type = s, .cpl = dpl, \
    .present = p, .limit_high = (limit) >> 16, .available = 1, .long_desc = is64, \
    .op_size = is32, .granularity = g, .base_high = (base) >> 24 \
  })

#define system_segment_high(base) \
  ((gdt_entry_t) { \
    .raw = (base) >> 32 \
  })

#define segment_type(b0, b1, b2, b3) \
  (((b0) << 0) | ((b1) << 1) | ((b2) << 2) | ((b3) << 3))

// segment types

#define null_segment() \
  segment(0, 0, 0, 0, 0, 0, 0, 0, 0)

#define code_segment(base, limit, dpl, read, c, is64, is32, g) \
  segment(base, limit, segment_type(0, read, c, 1), 1, dpl, 1, is64, is32, g)

#define data_segment(base, limit, dpl, write, e, is64, is32, g) \
  segment(base, limit, segment_type(0, write, e, 0), 1, dpl, 1, is64, is32, g)

// 64-bit segments

#define code_segment64(ring) \
  code_segment(0, 0, ring, 1, 0, 1, 0, 1)

#define data_segment64(ring) \
  data_segment(0, 0, ring, 1, 0, 0, 1, 1)

#define tss_segment_low(base) \
  system_segment_low(base, 0, segment_type(1, 0, 0, 1), 0, 0, 1, 0, 0, 1)

#define tss_segment_high(base) \
  system_segment_high(base)


typedef union packed gdt_entry {
  struct {
    uint16_t limit_low;      // Lower 16 bits of the limit
    uint16_t base_low;       // Lower 16 bits of the base
    uint8_t base_mid;        // Middle 8 bits of the base
    uint8_t type : 4;        // Segment type
    uint8_t desc_type : 1;   // Descriptor type (0 = system, 1 = code/data)
    uint8_t cpl : 2;         // Descriptor privilege level
    uint8_t present : 1;     // Segment present
    uint8_t limit_high : 4;  // Last 4 bits of the limit
    uint8_t available : 1;   // Available for system use
    uint8_t long_desc : 1;   // 64-bit code segment (IA-32e mode only)
    uint8_t op_size : 1;     // 0 = 16-bit | 1 = 32-bit
    uint8_t granularity : 1; // 0 = 1 B | 1 = 4 KiB
    uint8_t base_high;       // Last 8 bits of the base
  };
  uint64_t raw;
} gdt_entry_t;
static_assert(sizeof(gdt_entry_t) == sizeof(uint64_t));

struct packed tss {
  uint32_t : 32;      // reserved
  uint64_t rsp0;      // rsp0
  uint64_t rsp1;      // rsp1
  uint64_t rsp2;      // rsp2
  uint64_t : 64;      // reserved
  uint64_t ist1;      // ist1
  uint64_t ist2;      // ist2
  uint64_t ist3;      // ist3
  uint64_t ist4;      // ist4
  uint64_t ist5;      // ist5
  uint64_t ist6;      // ist6
  uint64_t ist7;      // ist7
  uint64_t : 32;      // reserved
  uint16_t : 16;      // reserved
  uint16_t iopb_ofst; // iopb offset
};

typedef struct packed {
  uint16_t limit;
  uint64_t base;
} gdt_desc_t;

gdt_entry_t bsp_gdt[] = {
  null_segment(),    // 0x00 null
  code_segment64(0), // 0x08 kernel code
  data_segment64(0), // 0x10 kernel data
  data_segment64(3), // 0x18 user data
  code_segment64(3), // 0x20 user code
  null_segment(),    // 0x28 tss low
  null_segment(),    // 0x30 tss high
};

uint8_t ring0_stack[4096];
struct tss tss;

void setup_gdt() {
  gdt_desc_t gdt_desc;
  if (PERCPU_IS_BSP) {
    memset((void *) &tss, 0, sizeof(tss));
    tss.rsp0 = (uintptr_t) ring0_stack;

    bsp_gdt[5] = tss_segment_low((uintptr_t) &tss);
    bsp_gdt[6] = tss_segment_high((uintptr_t) &tss);

    gdt_desc.limit = sizeof(bsp_gdt) - 1;
    gdt_desc.base = (uint64_t) &bsp_gdt;
  } else {
    gdt_entry_t *ap_gdt = kmalloc(sizeof(bsp_gdt));
    memcpy(ap_gdt, bsp_gdt, sizeof(bsp_gdt));

    ap_gdt[5] = tss_segment_low((uintptr_t) &tss);
    ap_gdt[6] = tss_segment_high((uintptr_t) &tss);

    gdt_desc.limit = sizeof(bsp_gdt) - 1;
    gdt_desc.base = (uint64_t) ap_gdt;
  }

  PERCPU_SET_CPU_GDT(gdt_desc.base);
  cpu_load_gdt(&gdt_desc);
  cpu_load_tr(0x28);
  cpu_reload_segments();
}

