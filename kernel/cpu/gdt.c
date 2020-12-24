//
// Created by Aaron Gill-Braun on 2019-04-24.
//

#include <cpu/cpu.h>
#include <cpu/gdt.h>
#include <string.h>

uint8_t stack[1024];

gdt_entry_t gdt[] = {
  null_segment(),    // 0x00 null
  code_segment64(0), // 0x08 kernel code
  data_segment64(0), // 0x10 kernel data
  data_segment64(3), // 0x18 user data
  code_segment64(3), // 0x20 user code
  null_segment(),    // 0x28 tss low
  null_segment(),    // 0x30 tss high
};

tss_t tss;

gdt_desc_t gdt_desc;

void setup_gdt() {
  memset((void *) &tss, 0, sizeof(tss));
  tss.rsp0 = (uintptr_t) stack;

  gdt[5] = tss_segment_low((uintptr_t) &tss);
  gdt[6] = tss_segment_high((uintptr_t) &tss);

  gdt_desc.limit = sizeof(gdt) - 1;
  gdt_desc.base = (uint64_t) &gdt;

  load_gdt(&gdt_desc);
  load_tr(0x28);
  flush_gdt();
}

