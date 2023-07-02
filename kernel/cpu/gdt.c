//
// Created by Aaron Gill-Braun on 2019-04-24.
//

#include <kernel/cpu/gdt.h>
#include <kernel/cpu/cpu.h>
#include <kernel/string.h>
#include <kernel/panic.h>
#include <kernel/mm.h>

struct packed gdt_desc {
  uint16_t limit;
  uint64_t base;
};

#define TSS_LOW  0x28ULL
#define TSS_HIGH 0x30ULL

#define index(v) ((v)/sizeof(gdt_entry_t))

gdt_entry_t bsp_gdt[] = {
  null_segment(),    // 0x00 null
  [index(KERNEL_CS)] = code_segment64(0), // 0x08 kernel code
  [index(KERNEL_DS)] = data_segment64(0), // 0x10 kernel data
  [index(USER_DS)]   = data_segment64(3), // 0x18 user data
  [index(USER_CS)]   = code_segment64(3), // 0x20 user code
  [index(TSS_LOW)]   = null_segment(),    // 0x28 tss low
  [index(TSS_HIGH)]  = null_segment(),    // 0x30 tss high
};

struct tss bsp_tss;

//

void setup_gdt() {
  struct gdt_desc gdt_desc;
  if (PERCPU_IS_BSP) {
    bsp_gdt[index(TSS_LOW)] = tss_segment_low((uintptr_t) &bsp_tss);
    bsp_gdt[index(TSS_HIGH)] = tss_segment_high((uintptr_t) &bsp_tss);
    gdt_desc.limit = sizeof(bsp_gdt) - 1;
    gdt_desc.base = (uint64_t) &bsp_gdt;
    PERCPU_SET_CPU_GDT(&bsp_gdt);
    PERCPU_SET_CPU_TSS(&bsp_tss);
  } else {
    struct tss *ap_tss = kmallocz(sizeof(struct tss));
    gdt_entry_t *ap_gdt = kmalloc(sizeof(bsp_gdt));
    memcpy(ap_gdt, bsp_gdt, sizeof(bsp_gdt));

    ap_gdt[index(TSS_LOW)] = tss_segment_low((uintptr_t) &ap_tss);
    ap_gdt[index(TSS_HIGH)] = tss_segment_high((uintptr_t) &ap_tss);
    gdt_desc.limit = sizeof(bsp_gdt) - 1;
    gdt_desc.base = (uint64_t) ap_gdt;
    PERCPU_SET_CPU_GDT(ap_gdt);
    PERCPU_SET_CPU_TSS(ap_tss);
  }

  cpu_load_gdt(&gdt_desc);
  cpu_load_tr(TSS_LOW);
  cpu_reload_segments();
}

uintptr_t tss_set_rsp(int cpl, uintptr_t sp) {
  kassert(cpl >= 0 && cpl <= 3);
  struct tss *tss = __percpu_get_cpu_tss();
  uintptr_t old_rsp = tss->rsp[cpl];
  tss->rsp[cpl] = sp;
  return old_rsp;
}

uintptr_t tss_set_ist(int ist, uintptr_t sp) {
  kassert(ist >= 1 && ist <= 7);
  struct tss *tss = __percpu_get_cpu_tss();
  uintptr_t old_rsp = tss->ist[ist - 1];
  tss->ist[ist - 1] = sp;
  return old_rsp;
}
