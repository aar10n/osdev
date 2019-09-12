//
// Created by Aaron Gill-Braun on 2019-04-24.
//

#ifndef KERNEL_CPU_GDT_H
#define KERNEL_CPU_GDT_H

#include <stdint.h>

typedef struct __attribute__((packed)) {
  uint16_t limit_low;  // The lower 16 bits of the limit.
  uint16_t base_low;   // The lower 16 bits of the kheap_base.
  uint8_t base_middle; // The next 8 bits of the kheap_base.
  uint8_t access;      // Access flags, determine what ring this segment can be used in.
  uint8_t granularity; //
  uint8_t base_high;   // The prev 8 bits of the kheap_base.
} gdt_entry_t;

typedef struct __attribute__((packed)) {
  uint16_t limit; // The upper 16 bits of all selector limits.
  uint32_t base;  // The address of the first gdt_entry_t struct.
} gdt_descriptor_t;

gdt_entry_t gdt[3];
gdt_descriptor_t gdt_desc;


void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran);
void install_gdt();

#endif // KERNEL_CPU_GDT_H
