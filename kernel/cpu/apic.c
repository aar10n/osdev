//
// Created by Aaron Gill-Braun on 2020-09-19.
//

#include <stdio.h>
#include <string.h>

#include <kernel/cpu/apic.h>
#include <kernel/cpu/asm.h>
#include <kernel/cpu/interrupt.h>
#include <kernel/cpu/rtc.h>
#include <kernel/mm/mm.h>

extern uintptr_t ap_start;
extern uintptr_t ap_end;
uintptr_t apic_base = 0xFEE000F0;

static uint32_t apic_read(uint32_t reg) {
  if (apic_base == 0) return 0;
  volatile uint32_t *apic = (uint32_t *) (apic_base + reg);
  return *apic;
}

static void apic_write(uint32_t reg, uint32_t value) {
  if (apic_base == 0) return;
  volatile uint32_t *apic = (uint32_t *) (apic_base + reg);
  *apic = value;
}

//

void svr_handler(registers_t regs) {
  kprintf("[apic] spurious interrupt\n");
}

//

void apic_init(uintptr_t local_apic_base) {
  apic_base = local_apic_base;

  // copy smp trampoline code to low memory
  uintptr_t dest = phys_to_virt(SMPBOOT_START);
  memcpy((void *) dest, &ap_start, (uintptr_t) &ap_end -  (uintptr_t)&ap_start);

  // ensure apic is enabled
  uint32_t svr = apic_read(APIC_REG_SVR);
  apic_write(APIC_REG_SVR, svr | (1 << 8));

  // uint32_t dfr = 0x00FFFFFF;
  // apic_write(APIC_REG_DFR, dfr);

  // uint32_t ldr = 0xF0000000;
  // apic_write(APIC_REG_LDR, ldr);

  // apic_write(APIC_REG_TPR, (2 << 4));
  //
  // apic_write(APIC_REG_ERROR, 0);

  // uint32_t icr0 = make_icr_low(0, APIC_INIT, 0, 0, 1, 0, 3);
  // apic_write(APIC_REG_ICR_LOW, icr0);
  // rtc_sleep(10);
  // uint8_t vector = SMPBOOT_START >> 12;
  // icr0 = make_icr_low(vector, APIC_START_UP, 0, 0, 1, 0, 3);
  // apic_write(APIC_REG_ICR_LOW, icr0);

  // rtc_sleep(1);
  // kprintf("Hello from the main core!\n");
  // uint32_t attempt = 0;
  // while (core_count < 2) {
  //   rtc_sleep(1);
  //
  //   if (attempt > 20) {
  //     kprintf("[apic] failed to wake core\n");
  //     break;
  //   }
  //   attempt++;
  // }

  apic_send_eoi();
}

void apic_send_eoi() {
  apic_write(APIC_REG_EOI, 0);
}
