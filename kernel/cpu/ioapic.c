//
// Created by Aaron Gill-Braun on 2020-09-20.
//

#include <kernel/cpu/ioapic.h>
#include <kernel/mem/heap.h>
#include <stdio.h>
#include <string.h>

static uint8_t ioapic_count = 0;
static ioapic_t *ioapics;
static uint8_t premapped[32] = {};

static uint32_t ioapic_read(uint8_t id, uint8_t reg) {
  volatile uint32_t *regsel = (uint32_t *) (ioapics[id].ioapic_addr + IOREGSEL);
  volatile uint32_t *regwin = (uint32_t *) (ioapics[id].ioapic_addr + IOREGWIN);
  *regsel = reg;
  return *regwin;
}

static void ioapic_write(uint8_t id, uint8_t reg, uint32_t value) {
  volatile uint32_t *regsel = (uint32_t *) (ioapics[id].ioapic_addr + IOREGSEL);
  volatile uint32_t *regwin = (uint32_t *) (ioapics[id].ioapic_addr + IOREGWIN);
  *regsel = reg;
  *regwin = value;
}

//

void ioapic_init(system_info_t *sysinfo) {
  ioapic_count = sysinfo->ioapic_count;
  ioapics = sysinfo->ioapics;

  if (ioapic_count == 0) {
    return;
  }

  for (int i = 0; i < ioapic_count; i++) {
    uint32_t version = ioapic_read(i, IOAPIC_REG_VERSION);
    uint8_t id = ioapics[i].ioapic_id;
    int max_irq = get_max_irq(version);
    ioapics[i].max_irq = max_irq;

    source_override_t *sources = ioapics[i].overrides;
    source_override_t *source = sources;

    memset(premapped, 0, sizeof(premapped));
    while (source) {
      if (ioapics[i].interrupt_base >= source->system_interrupt) {
        source = source->next;
        continue;
      }

      uint8_t vector = source->source_irq + 32;

      uint8_t active_low = (source->flags & 2) != 0;
      uint8_t trigger = (source->flags & 8) != 0;
      uint32_t rdr_low = make_rdrentry_low(vector, 0, 0, active_low, trigger, 1);
      uint32_t rdr_high = make_rdrentry_high(0);

      uint8_t index = rdrentry_index(source->system_interrupt);
      ioapic_write(id, index, rdr_low);
      ioapic_write(id, index + 1, rdr_high);

      kprintf("[ioapic] IRQ %d -> Pin %d\n", source->source_irq, source->system_interrupt);

      premapped[source->source_irq] = 1;
      premapped[source->system_interrupt] = 1;
      source = source->next;
    }

    for (int j = 0; j < max_irq; j++) {
      uint8_t index = rdrentry_index(j);
      if (premapped[j] == 1) {
        continue;
      }

      uint32_t rdr_low;
      if (j == 1) {
        rdr_low = make_rdrentry_low(254, 0, 0, 0, 0, 1);
      } else if (j == 8) {
        rdr_low = make_rdrentry_low(253, 0, 0, 0, 0, 1);
      } else {
        rdr_low = make_rdrentry_low(j + 32, 0, 0, 0, 0, 1);
      }

      uint32_t rdr_high = make_rdrentry_high(0);

      ioapic_write(id, index, rdr_low);
      ioapic_write(id, index + 1, rdr_high);

      kprintf("[ioapic] IRQ %d -> %d\n", j, j);
    }
  }

}

void ioapic_set_mask(uint8_t id, uint8_t pin, uint8_t mask) {
  uint8_t index = rdrentry_index(pin);
  uint32_t value = ioapic_read(id, index);
  uint32_t new_value = mask ? (value | (1 << 16)) : (value & ~(1 << 16));
  ioapic_write(id, index, new_value);
}
