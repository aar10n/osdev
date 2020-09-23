//
// Created by Aaron Gill-Braun on 2020-09-20.
//

#include <kernel/cpu/ioapic.h>
#include <kernel/mm/heap.h>
#include <stdio.h>
#include <string.h>

static uint8_t ioapic_count = 0;
static ioapic_desc_t *ioapics;
// static uint8_t premapped[32] = {};

static uint32_t ioapic_read(uint8_t id, uint8_t reg) {
  volatile uint32_t *regsel = (uint32_t *) (ioapics[id].address + IOREGSEL);
  volatile uint32_t *regwin = (uint32_t *) (ioapics[id].address + IOREGWIN);
  *regsel = reg;
  return *regwin;
}

static void ioapic_write(uint8_t id, uint8_t reg, uint32_t value) {
  volatile uint32_t *regsel = (uint32_t *) (ioapics[id].address + IOREGSEL);
  volatile uint32_t *regwin = (uint32_t *) (ioapics[id].address + IOREGWIN);
  *regsel = reg;
  *regwin = value;
}

static uint8_t ioapic_get_pin(uint8_t id, uint8_t irq) {
  ioapic_desc_t ioapic = ioapics[id];

  irq_source_t *source = ioapic.sources;
  while (source) {
    if (source->source_irq == irq) {
      // the standard irq is overriden
      return source->dest_interrupt;
    }
    source = source->next;
  }

  // the irq is mapped 1:1
  return irq;
}

//

void ioapic_init(system_info_t *sysinfo) {
  ioapic_count = sysinfo->ioapic_count;
  ioapics = sysinfo->ioapics;

  if (ioapic_count == 0) {
    return;
  }

  ioapics = sysinfo->ioapics;
  ioapic_count = sysinfo->ioapic_count;

  // for (int i = 0; i < ioapic_count; i++) {
  //   uint32_t version = ioapic_read(i, IOAPIC_REG_VERSION);
  //   uint8_t id = ioapics[i].id;
  //
  //   irq_source_t *sources = ioapics[i].sources;
  //   irq_source_t *source = sources;
  //
  //   // memset(premapped, 0, sizeof(premapped));
  //   // while (source) {
  //   //   if (ioapics[i].base >= source->dest_interrupt) {
  //   //     source = source->next;
  //   //     continue;
  //   //   }
  //   //
  //   //   uint8_t vector = source->source_irq + 32;
  //   //
  //   //   uint8_t active_low = (source->flags & 2) != 0;
  //   //   uint8_t trigger = (source->flags & 8) != 0;
  //   //   uint32_t rdr_low = make_rentry_low(vector, 0, 0, active_low, trigger, 1);
  //   //   uint32_t rdr_high = make_rentry_high(0);
  //   //
  //   //   uint8_t index = get_rentry_index(source->dest_interrupt);
  //   //   ioapic_write(id, index, rdr_low);
  //   //   ioapic_write(id, index + 1, rdr_high);
  //   //
  //   //   kprintf("[ioapic] IRQ %d -> Pin %d\n", source->source_irq, source->dest_interrupt);
  //   //
  //   //   premapped[source->source_irq] = 1;
  //   //   premapped[source->dest_interrupt] = 1;
  //   //   source = source->next;
  //   // }
  //
  //   // for (int j = 0; j < max_irq; j++) {
  //   //   uint8_t index = get_rentry_index(j);
  //   //   if (premapped[j] == 1) {
  //   //     continue;
  //   //   }
  //   //
  //   //   uint32_t rdr_low = make_rentry_low(j + 32, 0, 0, 0, 0, 1);
  //   //   uint32_t rdr_high = make_rentry_high(0);
  //   //
  //   //   ioapic_write(id, index, rdr_low);
  //   //   ioapic_write(id, index + 1, rdr_high);
  //   //
  //   //   kprintf("[ioapic] IRQ %d -> %d\n", j, j);
  //   // }
  // }

}

void ioapic_set_irq(uint8_t id, uint8_t irq, uint8_t apic_id, uint8_t vector) {
  uint8_t pin = ioapic_get_pin(id, irq);
  uint8_t index = get_rentry_index(pin);

  uint32_t high = ioapic_read(id, index + 1);
  high &= ~0xFF000000;
  high |= apic_id << 24;
  ioapic_write(id, index + 1, high);

  uint32_t low = ioapic_read(id, index);
  low &= ~(1 << 16); // unmask irq
  low &= ~(1 << 11); // physical delivery
  low &= ~0x700;     // fixed delivery
  low &= ~0xFF;
  low |= vector;     // set delivery vector

  ioapic_write(id, index, low);
}

void ioapic_set_mask(uint8_t id, uint8_t pin, uint8_t mask) {
  uint8_t index = get_rentry_index(pin);
  uint32_t value = ioapic_read(id, index);
  uint32_t new_value = mask ? (value | (1 << 16)) : (value & ~(1 << 16));
  ioapic_write(id, index, new_value);
}
