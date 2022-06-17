//
// Created by Aaron Gill-Braun on 2020-10-14.
//

#ifndef KERNEL_DEVICE_IOAPIC_H
#define KERNEL_DEVICE_IOAPIC_H

#include <base.h>
#include <cpu/cpu.h>

// delivery mode
#define IOAPIC_FIXED        0
#define IOAPIC_LOWEST_PRIOR 1
#define IOAPIC_SMI          2
#define IOAPIC_NMI          4
#define IOAPIC_INIT         5
#define IOAPIC_ExtINT       7

// destination mode
#define IOAPIC_DEST_PHYSICAL 0
#define IOAPIC_DEST_LOGICAL  1

// delivery status
#define IOAPIC_IDLE    1
#define IOAPIC_PENDING 2

// polarity
#define IOAPIC_ACTIVE_HIGH 0
#define IOAPIC_ACTIVE_LOW  1

// trigger mode
#define IOAPIC_EDGE  0
#define IOAPIC_LEVEL 1

typedef union packed ioapic_rentry {
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

void disable_legacy_pic();

void register_ioapic(uint8_t id, uint32_t address, uint32_t gsi_base);
void ioapic_set_irq_vector(uint8_t irq, uint8_t vector);
void ioapic_set_irq_dest(uint8_t irq, uint8_t mode, uint8_t dest);
void ioapic_set_irq_mask(uint8_t irq, bool mask);
void ioapic_set_irq_rentry(uint8_t irq, ioapic_rentry_t rentry);


#endif
