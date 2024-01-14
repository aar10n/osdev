//
// Created by Aaron Gill-Braun on 2019-04-24.
//

#ifndef KERNEL_CPU_GDT_H
#define KERNEL_CPU_GDT_H

#include <kernel/base.h>

#define entry(base, limit, s, typ, ring, p, is64, is32, g)  \
  ((union gdt_entry) {                                      \
    .limit_low = (limit) & 0xFFFF, .base_low = (base) & 0xFFFF, \
    .base_mid = (base) >> 16, .type = (typ), .desc_type = (s), .cpl = (ring), \
    .present = (p), .limit_high = (limit) >> 16, .available = 0, .long_desc = (is64), \
    .op_size = (is32), .granularity = (g), .base_high = (base) >> 24 \
  })
#define entry_extended(base) ((union gdt_entry){.raw = (base) >> 32})
#define entry_type(b8, b9, b10, b11) (((b8) << 0) | ((b9) << 1) | ((b10) << 2) | ((b11) << 3))


union gdt_entry {
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
};
static_assert(sizeof(union gdt_entry) == 8);

struct packed gdt_desc {
  uint16_t limit;
  uint64_t base;
};
static_assert(sizeof(struct gdt_desc) == 10);

struct packed tss {
  uint32_t : 32;      // reserved
  uint64_t rsp[3];    // rsp[0-2]
  uint64_t : 64;      // reserved
  uint64_t ist[7];    // ist[0-6]
  uint64_t : 32;      // reserved
  uint16_t : 16;      // reserved
  uint16_t iopb_ofst; // iopb offset
};
static_assert(sizeof(struct tss) == 100);

void tss_set_ist(int ist, uintptr_t sp);

#endif // KERNEL_CPU_GDT_H
