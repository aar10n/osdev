//
// Created by Aaron Gill-Braun on 2020-10-14.
//

#include <device/ioapic.h>

#include <cpu/io.h>
#include <init.h>
#include <mm.h>
#include <panic.h>

#define MAX_IOAPICS 16

#define IOREGSEL 0x00
#define IOREGWIN 0x10

#define get_rentry_index(irq) \
  (IOAPIC_RENTRY_BASE + ((irq) * 2))

typedef enum ioapic_reg {
  IOAPIC_ID          = 0x00,
  IOAPIC_VERSION     = 0x01,
  IOAPIC_ARB_ID      = 0x02,
  IOAPIC_RENTRY_BASE = 0x10
} ioapic_reg_t;

struct ioapic_device {
  uint8_t id;
  uint8_t used;
  uint8_t max_rentry;
  uint8_t version;
  uint32_t gsi_base;
  uintptr_t mmio_base;
  uintptr_t address;
};

static size_t num_ioapics = 0;
static struct ioapic_device ioapics[MAX_IOAPICS];

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

static int get_ioapic_for_interrupt(uint8_t irq) {
  for (size_t i = 0; i < num_ioapics; i++) {
    if (irq >= ioapics[i].gsi_base && irq < ioapics[i].gsi_base + ioapics[i].max_rentry) {
      return ioapics[i].id;
    }
  }
  return -1;
}

//

void remap_ioapic_registers(void *data) {
  struct ioapic_device *ioapic = data;
  ioapic->address = (uintptr_t) _vmap_mmio(ioapic->mmio_base, PAGE_SIZE, PG_WRITE | PG_NOCACHE);
  _vmap_name_mapping(ioapic->address, PAGE_SIZE, "ioapic");
}

//

void disable_legacy_pic() {
  outb(0x21, 0xFF); // legacy pic1
  outb(0xA1, 0xFF); // legacy pic2
}

void register_ioapic(uint8_t id, uint32_t address, uint32_t gsi_base) {
  kassert(id < MAX_IOAPICS);
  kassert(num_ioapics < MAX_IOAPICS);
  if (ioapics[id].used) {
    panic("ioapic %d already registered", id);
  }

  ioapics[id].id = id;
  ioapics[id].used = 1;
  ioapics[id].gsi_base = gsi_base;
  ioapics[id].mmio_base = address;
  ioapics[id].address = address;

  uint32_t version = ioapic_read(address, IOAPIC_VERSION);
  ioapics[id].max_rentry = (version >> 16) & 0xFF;
  ioapics[id].version = version & 0xFF;

  // mask all interrupts
  for (uint8_t i = 0; i < ioapics[id].max_rentry; i++) {
    ioapic_rentry_t rentry = read_rentry(id, get_rentry_index(i));
    rentry.mask = 1;
    write_rentry(id, get_rentry_index(i), rentry);
  }

  num_ioapics += 1;
  register_init_address_space_callback(remap_ioapic_registers, &ioapics[id]);
}

void ioapic_set_irq_vector(uint8_t irq, uint8_t vector) {
  int id = get_ioapic_for_interrupt(irq);
  if (id < 0) {
    panic("no ioapic for interrupt %d", irq);
  }

  ioapic_rentry_t rentry = read_rentry(id, get_rentry_index(irq));
  rentry.vector = vector;
}

void ioapic_set_irq_dest(uint8_t irq, uint8_t mode, uint8_t dest) {
  kassert(mode == IOAPIC_DEST_PHYSICAL || mode == IOAPIC_DEST_LOGICAL);
  int id = get_ioapic_for_interrupt(irq);
  if (id < 0) {
    panic("no ioapic for interrupt %d", irq);
  }

  ioapic_rentry_t rentry = read_rentry(id, get_rentry_index(irq));
  rentry.dest_mode = mode;
  rentry.dest = dest;
  write_rentry(id, get_rentry_index(irq), rentry);
}

void ioapic_set_irq_mask(uint8_t irq, bool mask) {
  int id = get_ioapic_for_interrupt(irq);
  if (id < 0) {
    panic("no ioapic for interrupt %d", irq);
  }

  ioapic_rentry_t rentry = read_rentry(id, get_rentry_index(irq));
  rentry.mask = mask & 1;
  write_rentry(id, get_rentry_index(irq), rentry);
}

void ioapic_set_irq_rentry(uint8_t irq, ioapic_rentry_t rentry) {
  int id = get_ioapic_for_interrupt(irq);
  if (id < 0) {
    panic("no ioapic for interrupt %d", irq);
  }

  write_rentry(id, get_rentry_index(irq), rentry);
}

