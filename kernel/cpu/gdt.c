//
// Created by Aaron Gill-Braun on 2019-04-24.
//

#include <kernel/cpu/asm.h>
#include <kernel/cpu/gdt.h>

gdt_entry_t gdt[] = {
  null_segment(),
  code_segment(0, 0xFFFFF, 1, 0, 1, 0),
  data_segment(0, 0xFFFFF, 1, 0, 1, 0),
};

gdt_descriptor_t gdt_desc;

void install_gdt() {
  gdt_desc.size = sizeof(gdt) - 1;
  gdt_desc.base = (uint32_t) &gdt;

  gdt[0] = null_segment();

  load_gdt(&gdt_desc);
}
