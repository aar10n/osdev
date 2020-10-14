//
// Created by Aaron Gill-Braun on 2019-04-24.
//

#include <cpu/cpu.h>
#include <cpu/gdt.h>
#include <string.h>

gdt_entry_t gdt[] = {
  null_segment(),
  code_segment64(0),
  data_segment64(0),
};

gdt_desc_t gdt_desc;

void setup_gdt() {
  gdt_desc.limit = sizeof(gdt) - 1;
  gdt_desc.base = (uint64_t) &gdt;

  load_gdt(&gdt_desc);
  flush_gdt();
}

