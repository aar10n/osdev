//
// Created by Aaron Gill-Braun on 2019-04-24.
//

#include <kernel/cpu/gdt.h>
#include <kernel/cpu/cpu.h>
#include <kernel/panic.h>
#include <kernel/mm.h>

#define system_segment(base, limit, s, typ, ring, p, is64, is32, g) \
  entry(base, limit, s, typ, ring, p, is64, is32, g)
#define code_segment(base, limit, s, r, c, ring, p, is64, is32, g) \
  entry(base, limit, s, entry_type(0, r, c, 1), ring, p, is64, is32, g)
#define data_segment(base, limit, s, w, x, ring, p, is64, is32, g) \
  entry(base, limit, s, entry_type(0, w, x, 0), ring, p, is64, is32, g)

#define null_segment() entry(0, 0, 0, 0, 0, 0, 0, 0, 0)
#define code_segment32(ring) code_segment(0, 0, 1, 1, 0, ring, 1, 0, 0, 1)
#define code_segment64(ring) code_segment(0, 0, 1, 1, 0, ring, 1, 1, 0, 1)
#define data_segment32(ring) data_segment(0, 0, 1, 1, 0, ring, 1, 0, 1, 1)
#define data_segment64(ring) data_segment(0, 0, 1, 1, 0, ring, 1, 1, 1, 1)
#define tss_segment64_lo(base) system_segment(base, 0, 0, entry_type(1, 0, 0, 1), 0, 1, 0, 0, 1)
#define tss_segment64_hi(base) entry_extended(base)

#define index(v) ((v)/sizeof(union gdt_entry))

struct tss cpu0_tss = {0};
union gdt_entry cpu0_gdt[] = {
  [index(NULL_SEG)] = null_segment(),
  [index(KCODE_SEG)] = code_segment64(0),
  [index(KDATA_SEG)] = data_segment64(0),
  [index(UCODE32_SEG)] = code_segment32(3),
  [index(UDATA_SEG)] = data_segment32(3),
  [index(UCODE64_SEG)] = code_segment64(3),
  [index(TSS_LO_SEG)] = null_segment(), // initialized later
  [index(TSS_HI_SEG)] = null_segment(), // initialized later
};

static void gdt_percpu_init() {
  // early initialization of the GDT and TSS for all CPUs
  union gdt_entry *gdt;
  struct tss *tss;
  if (curcpu_is_boot) {
    gdt = cpu0_gdt;
    tss = &cpu0_tss;
  } else {
    gdt = kmallocz(sizeof(cpu0_gdt));
    tss = kmallocz(sizeof(struct tss));
    // copy the GDT template for APs
    memcpy(gdt, cpu0_gdt, sizeof(cpu0_gdt));
  }

  gdt[index(TSS_LO_SEG)] = tss_segment64_lo((uintptr_t) tss);
  gdt[index(TSS_HI_SEG)] = tss_segment64_hi((uintptr_t) tss);

  struct gdt_desc desc;
  desc.limit = sizeof(cpu0_gdt) - 1;
  desc.base = (uint64_t) gdt;
  cpu_load_gdt(&desc);
  cpu_load_tr(TSS_LO_SEG);
  cpu_set_cs(KCODE_SEG);
  cpu_set_ds(KDATA_SEG);

  curcpu_area->gdt = gdt;
  curcpu_area->tss = tss;
  curcpu_area->tss_rsp0_ptr = &tss->rsp[0];
}
PERCPU_EARLY_INIT(gdt_percpu_init);

//

void tss_set_rsp(int idx, uintptr_t rsp) {
  kassert(idx >= 0 && idx <= 2);
  struct tss *tss = curcpu_area->tss;
  tss->rsp[idx] = rsp;
}

void tss_set_ist(int ist, uintptr_t sp) {
  kassert(ist >= 1 && ist <= 7);
  struct tss *tss = curcpu_area->tss;
  tss->ist[ist-1] = sp;
}
