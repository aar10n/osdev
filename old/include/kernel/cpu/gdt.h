//
// Created by Aaron Gill-Braun on 2019-04-24.
//

#ifndef KERNEL_CPU_GDT_H
#define KERNEL_CPU_GDT_H

#include <stdint.h>

#define segment(base, limit, typ, s, privl, pr, sz, gr)             \
  (gdt_entry_t) {                                                   \
    .limit_low = (limit) & 0xFFFF,                                  \
    .base_low = (base) & 0xFFFF,                                    \
    .base_mid = ((base) >> 16) & 0xFF,                              \
    .type = typ,                                                    \
    .desc_type = s,                                                 \
    .privilege = privl,                                             \
    .present = pr,                                                  \
    .limit_high = ((limit) >> 16) & 0xF,                            \
    .reserved = 0,                                                  \
    .size = sz,                                                     \
    .granularity = gr,                                              \
    .base_high = ((base) >> 24) & 0xFF,                             \
  }

#define segment_access(ac, rw, dc, ex) \
  ((ac) | ((rw) << 1) | ((dc) << 2) | ((ex) << 3))

// segment types

#define null_segment() \
  segment(0, 0, 0, 0, 0, 0, 0, 0)

#define data_segment(base, limit, gran, privl, write, direction) \
  segment(base, limit, segment_access(0, write, direction, 0), 1, privl, 1, 1, gran)

#define code_segment(base, limit, gran, privl, read, conform) \
  segment(base, limit, segment_access(0, read, conform, 1), 1, privl, 1, 1, gran)

#define system_segment(base, limit, gran, privl, type) \
  segment(base, limit, type, 0, privl, 1, 0, gran)

typedef struct __attribute__((packed)) {
  uint16_t limit_low;      // Lower 16 bits of the limit
  uint16_t base_low;       // Lower 16 bits of the base
  uint8_t base_mid;        // Middle 8 bits of the base
  uint8_t type : 4;        //
  uint8_t desc_type : 1;   //
  uint8_t privilege : 2;   //
  uint8_t present : 1;     //
  uint8_t limit_high : 4;  // Last 4 bits of the limit
  uint8_t reserved : 2;    //
  uint8_t size : 1;        // 0 = 16-bit | 1 = 32-bit
  uint8_t granularity : 1; // 0 = 1 B | 1 = 4 KiB
  uint8_t base_high;       // Last 8 bits of the base
} gdt_entry_t;

typedef struct __attribute__((packed)) {
  uint16_t size; // The size of the gdt (in bytes) minus 1
  uint32_t base; // The base address of the gdt
} gdt_descriptor_t;

void install_gdt();

#endif // KERNEL_CPU_GDT_H
