//
// Created by Aaron Gill-Braun on 2019-04-24.
//

#include <kernel/cpu/asm.h>
#include <kernel/cpu/gdt.h>

void install_gdt() {
  gdt_desc.limit = (sizeof(gdt_entry_t) * 5) - 1;
  gdt_desc.base = (uint32_t) &gdt;

  gdt_set_gate(0, 0, 0, 0, 0);                // Null segment
  gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); // Code segment
  gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF); // Data segment

  load_gdt(&gdt_desc);
}

void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
  gdt[num].base_low = (base & 0xFFFF);
  gdt[num].base_middle = (base >> 16) & 0xFF;
  gdt[num].base_high = (base >> 24) & 0xFF;

  gdt[num].limit_low = (limit & 0xFFFF);
  gdt[num].granularity = (limit >> 16) & 0x0F;

  gdt[num].granularity |= gran & 0xF0;
  gdt[num].access = access;
}
