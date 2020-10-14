//
// Created by Aaron Gill-Braun on 2019-04-24.
//

#ifndef KERNEL_CPU_GDT_H
#define KERNEL_CPU_GDT_H

#include <base.h>

#define mask1(val) ((val##L) & 1)
#define mask2(val) ((val##L) & 0b11)
#define mask8(val) ((val##L) & 0xFF)
#define mask16(val) ((val##L) & 0xFFFF)
#define shrmask4(val, shr) (((val##L) >> (shr)) & 0xF)
#define shrmask8(val, shr) (((val##L) >> (shr)) & 0xFF)

#define segment(base, limit, type, s, dpl, p, flags) \
  ((gdt_entry_t){                                    \
    .raw = (mask16(limit) | (mask16(base) << 16) | (shrmask8(base, 16) << 32) | \
           (type << 40) | (mask1(s) << 44) | (mask2(dpl) << 45) | \
           (mask1(p) << 47) | (shrmask4(limit, 16) << 48) | (flags << 52) | \
           (shrmask8(base, 24) << 56)) \
  })

#define segment_type(rw, ec, type) \
  ((mask1(rw) << 1) | (mask1(ec) << 2) | (mask1(type) << 3))

#define segment_flags(is64, is32, g) \
  ((mask1(is64) << 1) | (mask1(is32) << 2) | (mask1(g) << 3))

// segment types

#define null_segment() \
  segment(0L, 0L, 0L, 0L, 0L, 0L, 0L)

#define code_segment(base, limit, dpl, read, c, is64, is32, g) \
  segment(base, limit, segment_type(read, c, 1), 1, dpl, 1, segment_flags(is64, is32, g))

#define data_segment(base, limit, dpl, write, e, is64, is32, g) \
  segment(base, limit, segment_type(write, e, 0), 1, dpl, 1, segment_flags(is64, is32, g))

#define code_segment64(ring) \
  code_segment(0, 0xFFFFF, ring, 1, 0, 1, 0, 1)

#define data_segment64(ring) \
  data_segment(0, 0xFFFFF, ring, 1, 0, 0, 1, 1)



typedef union {
  uint64_t raw;
  struct packed {
    uint16_t limit_low;      // Lower 16 bits of the limit
    uint16_t base_low;       // Lower 16 bits of the base
    uint8_t base_mid;        // Middle 8 bits of the base
    uint8_t type : 4;        // Segment type
    uint8_t desc_type : 1;   // Descriptor type (0 = system, 1 = code/data)
    uint8_t dpl : 2;         // Descriptor privilege level
    uint8_t present : 1;     // Segment present
    uint8_t limit_high : 4;  // Last 4 bits of the limit
    uint8_t available : 1;   // Available for system use
    uint8_t long_desc : 1;   // 64-bit code segment (IA-32e mode only)
    uint8_t op_size : 1;     // 0 = 16-bit | 1 = 32-bit
    uint8_t granularity : 1; // 0 = 1 B | 1 = 4 KiB
    uint8_t base_high;       // Last 8 bits of the base
  };
} gdt_entry_t;

typedef struct packed {
  uint16_t limit;
  uint64_t base;
} gdt_desc_t;

void setup_gdt();

#endif // KERNEL_CPU_GDT_H
