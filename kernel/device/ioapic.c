//
// Created by Aaron Gill-Braun on 2020-10-14.
//

#include <acpi.h>
#include <panic.h>
#include <string.h>
#include <stdio.h>
#include <device/ioapic.h>

static uint8_t ioapic_count = 0;
static ioapic_desc_t *ioapics;
static bool premapped[32] = {};

static uint32_t ioapic_read(uint8_t id, uint8_t reg) {
  kassert(ioapics[id].virt_addr != 0);
  volatile uint32_t *regsel = (uint32_t *) (ioapics[id].virt_addr + IOREGSEL);
  volatile uint32_t *regwin = (uint32_t *) (ioapics[id].virt_addr + IOREGWIN);
  *regsel = reg;
  return *regwin;
}

static void ioapic_write(uint8_t id, uint8_t reg, uint32_t value) {
  kassert(ioapics[id].virt_addr != 0);
  volatile uint32_t *regsel = (uint32_t *) (ioapics[id].virt_addr + IOREGSEL);
  volatile uint32_t *regwin = (uint32_t *) (ioapics[id].virt_addr + IOREGWIN);
  *regsel = reg;
  *regwin = value;
}

static ioapic_rentry_t read_rentry(uint8_t id, uint8_t index) {
  ioapic_rentry_t rentry;
  rentry.raw_lower = ioapic_read(id, index);
  rentry.raw_upper = ioapic_read(id, index + 1);
  return rentry;
}

static void write_rentry(uint8_t id, uint8_t index, ioapic_rentry_t rentry) {
  ioapic_write(id, index, rentry.raw_lower);
  ioapic_write(id, index + 1, rentry.raw_upper);
}

//

static uint8_t ioapic_get_pin(uint8_t id, uint8_t irq) {
  ioapic_desc_t ioapic = ioapics[id];

  irq_source_t *source = ioapic.sources;
  while (source) {
    if (source->source_irq == irq) {
      // the standard irq is overriden
      return source->dest_int;
    }
    source = source->next;
  }
  // the irq is mapped 1:1
  return irq;
}

//

void ioapic_init() {
  kprintf("[ioapic] initializing\n");

  ioapic_count = system_info->ioapic_count;
  ioapics = system_info->ioapics;
  if (ioapic_count == 0) {
    return;
  }

  for (int i = 0; i < ioapic_count; i++) {
    ioapic_desc_t ioapic = ioapics[i];

    irq_source_t *sources = ioapics[i].sources;
    irq_source_t *source = sources;

    memset(premapped, 0, sizeof(premapped));
    while (source) {
      if (ioapics[i].int_base >= source->dest_int) {
        source = source->next;
        continue;
      }

      uint8_t vector = source->source_irq + 32;

      uint8_t active_low = (source->flags & 2) != 0;
      uint8_t trigger = (source->flags & 8) != 0;

      ioapic_rentry_t rentry = ioapic_rentry(
        vector, IOAPIC_FIXED, IOAPIC_PHYSICAL, IOAPIC_IDLE,
        active_low, 0, trigger, 1, 1
      );

      // source -> dest
      uint8_t index = get_rentry_index(source->dest_int);
      ioapic_write(ioapic.id, index, rentry.raw_lower);
      ioapic_write(ioapic.id, index + 1, rentry.raw_upper);
      premapped[source->source_irq] = true;
      kprintf("[ioapic] IRQ %d -> Pin %d\n", source->source_irq, source->dest_int);

      source = source->next;
    }

    for (int j = ioapic.int_base; j < ioapic.max_rentry; j++) {
      uint8_t index = get_rentry_index(j);
      if (premapped[j] == true) {
        continue;
      }

      ioapic_rentry_t rentry = ioapic_rentry(
        j + 32, IOAPIC_FIXED, IOAPIC_PHYSICAL, IOAPIC_IDLE,
        IOAPIC_ACTIVE_HIGH, 0, IOAPIC_EDGE, 1, 0
      );

      ioapic_write(ioapic.id, index, rentry.raw_lower);
      ioapic_write(ioapic.id, index + 1, rentry.raw_upper);

      kprintf("[ioapic] IRQ %d -> Pin %d\n", j, j);
    }
  }

  kprintf("[ioapic] done!\n");
}

void ioapic_set_irq(uint8_t id, uint8_t irq, uint8_t vector) {
  uint8_t pin = ioapic_get_pin(id, irq);
  uint8_t index = get_rentry_index(pin);

  ioapic_rentry_t rentry = read_rentry(id, index);
  rentry.vector = vector;
  rentry.mask = 0;
  write_rentry(id, index, rentry);
}

void ioapic_set_mask(uint8_t id, uint8_t irq, uint8_t mask) {
  uint8_t pin = ioapic_get_pin(id, irq);
  uint8_t index = get_rentry_index(pin);
  ioapic_rentry_t rentry = read_rentry(id, index);
  rentry.mask = mask;
  write_rentry(id, index, rentry);
}
