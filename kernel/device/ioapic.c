//
// Created by Aaron Gill-Braun on 2020-10-14.
//

#include <device/ioapic.h>

#include <mm.h>
#include <init.h>
#include <panic.h>
#include <printf.h>

#include <cpu/io.h>

#define MAX_IOAPICS 16

#define IOREGSEL 0x00
#define IOREGWIN 0x10

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

#define get_rentry_index(irq) \
  (IOAPIC_RENTRY_BASE + ((irq) * 2))

#define ioapic_rentry(vec, dm, dsm, ds, p, i, tm, m, dst) \
  ((ioapic_rentry_t){                                      \
    .vector = vec, .deliv_mode = dm, .dest_mode = dsm, .deliv_status = ds, \
    .polarity = p, .remote_irr = i, .trigger_mode = tm, .mask = m, .dest = dst \
  })

typedef enum ioapic_reg {
  IOAPIC_ID          = 0x00,
  IOAPIC_VERSION     = 0x01,
  IOAPIC_ARB_ID      = 0x02,
  IOAPIC_RENTRY_BASE = 0x10
} ioapic_reg_t;

typedef union packed ioapic_rentry {
  uint64_t raw;
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

struct ioapic_device {
  uint8_t id;
  uint8_t used;
  uint8_t max_rentry;
  uint8_t version;
  uint32_t gsi_base;
  uintptr_t phys_addr;
  uintptr_t address;
};

static size_t num_ioapics = 0;
static struct ioapic_device ioapics[MAX_IOAPICS];

static uint32_t ioapic_read(uint8_t id, ioapic_reg_t reg) {
  uintptr_t address = ioapics[id].address;
  volatile uint32_t *regsel = (uint32_t *) (address + IOREGSEL);
  volatile uint32_t *regwin = (uint32_t *) (address + IOREGWIN);
  *regsel = reg;
  return *regwin;
}

static void ioapic_write(uint8_t id, ioapic_reg_t reg, uint32_t value) {
  uintptr_t address = ioapics[id].address;
  volatile uint32_t *regsel = (uint32_t *) (address + IOREGSEL);
  volatile uint32_t *regwin = (uint32_t *) (address + IOREGWIN);
  *regsel = reg;
  *regwin = value;
}

static uint64_t ioapic_read64(uint8_t id, uint8_t index) {
  uintptr_t address = ioapics[id].address;
  volatile uint32_t *regsel = (uint32_t *) (address + IOREGSEL);
  volatile uint32_t *regwin = (uint32_t *) (address + IOREGWIN);

  *regsel = index;
  uint64_t value = *regwin;
  *regsel = index + 1;
  value |= ((uint64_t)(*regwin)) << 32;
  return value;
}

static void ioapic_write64(uint8_t id, uint8_t index, uint64_t value) {
  uintptr_t address = ioapics[id].address;
  volatile uint32_t *regsel = (uint32_t *) (address + IOREGSEL);
  volatile uint32_t *regwin = (uint32_t *) (address + IOREGWIN);

  *regsel = index;
  *regwin = (uint32_t)(value & UINT32_MAX);
  *regsel = index + 1;
  *regwin = (uint32_t)((value >> 32) & UINT32_MAX);
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
  ioapic->address = (uintptr_t) _vmap_mmio(ioapic->phys_addr, PAGE_SIZE, PG_WRITE | PG_NOCACHE);
  _vmap_get_mapping(ioapic->address)->name = "ioapic";
}

//

int ioapic_get_max_remappable_irq() {
  int max_irq = 0;
  for (int i = 0; i < MAX_IOAPICS; i++) {
    if (!ioapics[i].used) {
      continue;
    }

    int irq_base = ioapics[i].gsi_base;
    int irq_max = irq_base + ioapics[i].max_rentry;
    if (irq_max > max_irq) {
      max_irq = irq_max;
    }
  }

  return max_irq;
}

int ioapic_set_isa_irq_routing(uint8_t isa_irq, uint8_t vector, uint16_t flags) {
  if (isa_irq >= 16) {
    panic("ioapic_set_isa_irq_routing: isa_irq %d out of range", isa_irq);
  }

  int ioapic_id = get_ioapic_for_interrupt(isa_irq);
  if (ioapic_id < 0) {
    kprintf("no ioapic for interrupt %d", isa_irq);
    return -ENODEV;
  }

  ioapic_rentry_t rentry = { .raw = ioapic_read64(ioapic_id, get_rentry_index(isa_irq)) };
  rentry.mask = 1;
  rentry.vector = vector;

  uint8_t polarity = flags & 0x3;
  if (polarity == 0b00 || polarity == 0b11) {
    rentry.polarity = IOAPIC_ACTIVE_LOW;
  } else if (polarity == 0b01) {
    rentry.polarity = IOAPIC_ACTIVE_HIGH;
  } else {
    panic("ioapic_set_isa_irq_routing: invalid polarity %d", polarity);
  }

  uint8_t trigger_mode = (flags >> 2) & 0x3;
  if (trigger_mode == 0b00 || trigger_mode == 0b01) {
    rentry.trigger_mode = IOAPIC_EDGE;
  } else if (trigger_mode == 0b11) {
    rentry.trigger_mode = IOAPIC_LEVEL;
  } else {
    panic("ioapic_set_isa_irq_routing: invalid trigger mode %d", trigger_mode);
  }

  ioapic_write64(ioapic_id, get_rentry_index(isa_irq), rentry.raw);
  return 0;
}

int ioapic_set_irq_vector(uint8_t irq, uint8_t vector) {
  int id = get_ioapic_for_interrupt(irq);
  if (id < 0) {
    kprintf("no ioapic for interrupt %d", irq);
    return -ENODEV;
  }

  ioapic_rentry_t rentry = { .raw = ioapic_read64(id, get_rentry_index(irq)) };
  rentry.vector = vector;
  ioapic_write64(id, get_rentry_index(irq), rentry.raw);
  return 0;
}

int ioapic_set_irq_dest(uint8_t irq, uint8_t mode, uint8_t dest) {
  kassert(mode == IOAPIC_DEST_PHYSICAL || mode == IOAPIC_DEST_LOGICAL);
  int id = get_ioapic_for_interrupt(irq);
  if (id < 0) {
    kprintf("no ioapic for interrupt %d", irq);
    return -ENODEV;
  }

  ioapic_rentry_t rentry = { .raw = ioapic_read64(id, get_rentry_index(irq)) };
  rentry.dest_mode = mode;
  rentry.dest = dest;
  ioapic_write64(id, get_rentry_index(irq), rentry.raw);
  return 0;
}

int ioapic_set_irq_mask(uint8_t irq, bool mask) {
  int id = get_ioapic_for_interrupt(irq);
  if (id < 0) {
    kprintf("no ioapic for interrupt %d", irq);
    return -ENODEV;
  }

  ioapic_rentry_t rentry = { .raw = ioapic_read64(id, get_rentry_index(irq)) };
  rentry.mask = mask & 1;
  ioapic_write64(id, get_rentry_index(irq), rentry.raw);
  return 0;
}

//

void register_ioapic(uint8_t id, uintptr_t address, uint32_t gsi_base) {
  if (id >= MAX_IOAPICS || num_ioapics >= MAX_IOAPICS) {
    kprintf("IOAPIC: ignoring ioapic %d, not supported\n", id);
    return;
  } else if (ioapics[id].used) {
    panic("ioapic %d already registered", id);
  }

  ioapics[id].id = id;
  ioapics[id].used = 1;
  ioapics[id].gsi_base = gsi_base;
  ioapics[id].phys_addr = address;
  ioapics[id].address = address;

  uint32_t version = ioapic_read(address, IOAPIC_VERSION);
  ioapics[id].max_rentry = (version >> 16) & 0xFF;
  ioapics[id].version = version & 0xFF;

  // mask all interrupts
  for (uint8_t i = 0; i < ioapics[id].max_rentry; i++) {
    ioapic_rentry_t rentry = { .raw = ioapic_read64(id, get_rentry_index(i)) };
    rentry.mask = 1;
    ioapic_write64(id, get_rentry_index(i), rentry.raw);
  }

  num_ioapics += 1;
  register_init_address_space_callback(remap_ioapic_registers, &ioapics[id]);
}

void disable_legacy_pic() {
  outb(0x21, 0xFF); // legacy pic1
  outb(0xA1, 0xFF); // legacy pic2
}
