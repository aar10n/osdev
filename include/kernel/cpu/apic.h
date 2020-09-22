//
// Created by Aaron Gill-Braun on 2020-09-19.
//

#ifndef KERNEL_CPU_APIC_H
#define KERNEL_CPU_APIC_H

#include <stdint.h>

#define SMPBOOT_START 0x7000

#define IA32_APIC_BASE        0x1B
#define IA32_APIC_BASE_BSP    0x100
#define IA32_APIC_BASE_ENABLE 0x800

#define APIC_REG_ID        0x20
#define APIC_REG_VERSION   0x30
#define APIC_REG_TPR       0x80
#define APIC_REG_APR       0x90
#define APIC_REG_PPR       0xA0
#define APIC_REG_EOI       0xB0
#define APIC_REG_RRD       0xC0
#define APIC_REG_LDR       0xD0
#define APIC_REG_DFR       0xE0
#define APIC_REG_SVR       0xF0
// 0x100-0x170 In-service register
// 0x180-0x1F0 Trigger mode register
// 0x200-0x270 Interrupt request register
#define APIC_REG_ERROR     0x280
// 0x290-0x2E0 Reserved
#define APIC_REG_LVT_CMCI  0x2F0

#define APIC_REG_ICR_LOW   0x300
#define APIC_REG_ICR_HIGH  0x310
#define APIC_REG_LVT_TIMER 0x320

#define APIC_REG_LVT_LINT0 0x350
#define APIC_REG_LVT_LINT1 0x360
#define APIC_REG_LVT_ERROR 0x370

#define APIC_REG_INITIAL_COUNT 0x380
#define APIC_REG_CURRENT_COUNT 0x390

#define APIC_REG_DIVIDE_CONFIG 0x3E0

// Interrupt Command Register

#define make_icr_low(vec, dl_mode, ds_mode, dl_st, lvl, trig_mode, dst_short) \
  ((vec) | ((dl_mode) << 8) | ((ds_mode) << 11) | ((dl_st) << 12) | \
   ((lvl) << 14) | ((trig_mode) << 15) | ((dst_short) << 18))

#define make_icr_high(dest) \
  ((dest) << 24)

#define make_svr(vec, enable, fpc, eoi_supress) \
  ((vec) | ((enable) << 8) | ((fpc) << 9) | ((eoi_supress) << 12))

#define APIC_FIXED        0b000
#define APIC_LOW_PRIORITY 0b001
#define APIC_SMI          0b010
#define APIC_NMI          0b100
#define APIC_INIT         0b101
#define APIC_START_UP     0b110
#define APIC_ExtINT       0b111

//

void apic_init(uintptr_t apic_base);
void apic_send_eoi();

#endif
