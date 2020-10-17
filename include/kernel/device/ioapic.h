//
// Created by Aaron Gill-Braun on 2020-10-14.
//

#ifndef KERNEL_DEVICE_IOAPIC_H
#define KERNEL_DEVICE_IOAPIC_H

#include <base.h>
#include <cpu/cpu.h>

#define IOREGSEL 0x00
#define IOREGWIN 0x10

#define get_rentry_index(irq) \
  (0x10 + ((irq) * 2))

typedef enum {
  IOAPIC_REG_ID      = 0x00,
  IOAPIC_REG_VERSION = 0x01,
  IOAPIC_REG_ARB_ID  = 0x02,
  IOAPIC_REG_END     = 0x40
} ioapic_reg_t;

#define IOAPIC_FIXED        0
#define IOAPIC_LOWEST_PRIOR 1
#define IOAPIC_SMI          2
#define IOAPIC_NMI          4
#define IOAPIC_INIT         5
#define IOAPIC_ExtINT       7

#define IOAPIC_PHYSICAL 0
#define IOAPIC_LOGICAL  1

#define IOAPIC_IDLE    1
#define IOAPIC_PENDING 2

#define IOAPIC_ACTIVE_HIGH 0
#define IOAPIC_ACTIVE_LOW  1

#define IOAPIC_EDGE  0
#define IOAPIC_LEVEL 1


typedef union packed {
  uint32_t raw_lower;
  uint32_t raw_upper;
  struct {
    uint64_t vector : 8;
    uint64_t deliv_mode : 3;
    uint64_t dest_mode : 1;
    uint64_t deliv_status : 1;
    uint64_t polarity : 1;
    uint64_t remote_irr : 1;
    uint64_t trigger_mode : 1;
    uint64_t mask : 1;
    uint64_t : 39;
    uint64_t dest : 8;
  };
} ioapic_rentry_t;
#define ioapic_rentry(vec, dm, dsm, ds, p, i, tm, m, dst) \
  ((ioapic_rentry_t){                                      \
    .vector = vec, .deliv_mode = dm, .dest_mode = dsm, .deliv_status = ds, \
    .polarity = p, .remote_irr = i, .trigger_mode = tm, .mask = m, .dest = dst \
  })

void ioapic_init();
void ioapic_set_irq(uint8_t id, uint8_t irq, uint8_t vector);
void ioapic_set_mask(uint8_t id, uint8_t irq, uint8_t mask);

#endif
