//
// Created by Aaron Gill-Braun on 2020-10-14.
//

#include <acpi.h>
#include <stdio.h>
#include <vectors.h>
#include <device/apic.h>

static inline uint32_t apic_read(apic_reg_t reg) {
  uintptr_t addr = APIC_BASE_VA + reg;
  volatile uint32_t *apic = (uint32_t *) addr;
  return *apic;
}

static inline void apic_write(apic_reg_t reg, uint32_t value) {
  uintptr_t addr = APIC_BASE_VA + reg;
  volatile uint32_t *apic = (uint32_t *) addr;
  *apic = value;
}

//

void apic_init() {
  kprintf("[apic] initializing\n");

  apic_reg_tpr_t tpr = apic_reg_tpr(0, 0);
  apic_write(APIC_TPR, tpr.raw);

  apic_reg_ldr_t ldr = apic_reg_ldr(0xFF);
  apic_write(APIC_LDR, ldr.raw);

  apic_reg_dfr_t dfr = apic_reg_dfr(APIC_FLAT_MODEL);
  apic_write(APIC_DFR, dfr.raw);

  apic_reg_lvt_timer_t timer = apic_reg_lvt_timer(
    VECTOR_APIC_TIMER, APIC_IDLE, 1, APIC_ONE_SHOT
  );
  apic_write(APIC_LVT_TIMER, timer.raw);

  apic_reg_lvt_lint_t lint = apic_reg_lvt_lint(
    0, APIC_FIXED, APIC_IDLE, 0, 1, 0, APIC_LEVEL
  );

  lint.vector = VECTOR_APIC_LINT0;
  apic_write(APIC_LVT_LINT0, lint.raw);
  lint.vector = VECTOR_APIC_LINT1;
  apic_write(APIC_LVT_LINT1, lint.raw);

  apic_reg_svr_t svr = apic_reg_svr(VECTOR_APIC_SPURIOUS, 1, 0);
  apic_write(APIC_SVR, svr.raw);

  apic_send_eoi();
  kprintf("[apic] done!\n");
}

void apic_send_eoi() {
  apic_write(APIC_EOI, 0);
}
