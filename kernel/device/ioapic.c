//
// Created by Aaron Gill-Braun on 2020-10-14.
//

#include <kernel/device/ioapic.h>

#include <kernel/mm.h>
#include <kernel/init.h>
#include <kernel/panic.h>
#include <kernel/printf.h>

#include <kernel/cpu/io.h>

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
  uint8_t max_rentry;
  uint8_t version;
  uint32_t gsi_base;
  uintptr_t phys_addr;
  uintptr_t address;

  LIST_ENTRY(struct ioapic_device) list;
};

static size_t num_ioapics = 0;
static LIST_HEAD(struct ioapic_device) ioapics;

static uint32_t ioapic_read(uintptr_t address, ioapic_reg_t reg) {
  volatile uint32_t *regsel = (uint32_t *) (address + IOREGSEL);
  volatile uint32_t *regwin = (uint32_t *) (address + IOREGWIN);
  *regsel = reg;
  return *regwin;
}

static void ioapic_write(uintptr_t address, ioapic_reg_t reg, uint32_t value) {
  volatile uint32_t *regsel = (uint32_t *) (address + IOREGSEL);
  volatile uint32_t *regwin = (uint32_t *) (address + IOREGWIN);
  *regsel = reg;
  *regwin = value;
}

static uint64_t ioapic_read64(uintptr_t address, uint8_t index) {
  volatile uint32_t *regsel = (uint32_t *) (address + IOREGSEL);
  volatile uint32_t *regwin = (uint32_t *) (address + IOREGWIN);

  *regsel = index;
  uint64_t value = *regwin;
  *regsel = index + 1;
  value |= ((uint64_t)(*regwin)) << 32;
  return value;
}

static void ioapic_write64(uintptr_t address, uint8_t index, uint64_t value) {
  volatile uint32_t *regsel = (uint32_t *) (address + IOREGSEL);
  volatile uint32_t *regwin = (uint32_t *) (address + IOREGWIN);

  *regsel = index;
  *regwin = (uint32_t)(value & UINT32_MAX);
  *regsel = index + 1;
  *regwin = (uint32_t)((value >> 32) & UINT32_MAX);
}

static struct ioapic_device *get_ioapic_by_id(uint8_t id) {
  struct ioapic_device *ioapic;
  LIST_FOREACH(ioapic, &ioapics, list) {
    if (ioapic->id == id) {
      return ioapic;
    }
  }

  return NULL;
}

static struct ioapic_device *get_ioapic_for_interrupt(uint8_t irq) {
  struct ioapic_device *ioapic;
  LIST_FOREACH(ioapic, &ioapics, list) {
    if (irq >= ioapic->gsi_base && irq < ioapic->gsi_base + ioapic->max_rentry) {
      return ioapic;
    }
  }

  return NULL;
}

//

void remap_ioapic_registers(void *data) {
  struct ioapic_device *ioapic = data;
  ioapic->address = vm_alloc_map_phys(ioapic->phys_addr, 0, PAGE_SIZE, 0, PG_WRITE | PG_NOCACHE, "ioapic")->address;
}

//

int ioapic_get_max_remappable_irq() {
  int max_irq = 0;

  struct ioapic_device *ioapic;
  LIST_FOREACH(ioapic, &ioapics, list) {
    int irq_max = (int) ioapic->gsi_base + ioapic->max_rentry;
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

  struct ioapic_device *ioapic = get_ioapic_for_interrupt(isa_irq);
  if (ioapic == NULL) {
    kprintf("no ioapic for interrupt %d", isa_irq);
    return -ENODEV;
  }

  ioapic_rentry_t rentry = { .raw = ioapic_read64(ioapic->address, get_rentry_index(isa_irq)) };
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

  ioapic_write64(ioapic->address, get_rentry_index(isa_irq), rentry.raw);
  return 0;
}

int ioapic_set_irq_vector(uint8_t irq, uint8_t vector) {
  struct ioapic_device *ioapic = get_ioapic_for_interrupt(irq);
  if (ioapic == NULL) {
    kprintf("no ioapic for interrupt %d", irq);
    return -ENODEV;
  }

  ioapic_rentry_t rentry = { .raw = ioapic_read64(ioapic->address, get_rentry_index(irq)) };
  rentry.trigger_mode = IOAPIC_EDGE;
  rentry.vector = vector;
  ioapic_write64(ioapic->address, get_rentry_index(irq), rentry.raw);
  return 0;
}

int ioapic_set_irq_dest(uint8_t irq, uint8_t mode, uint8_t dest) {
  kassert(mode == IOAPIC_DEST_PHYSICAL || mode == IOAPIC_DEST_LOGICAL);
  struct ioapic_device *ioapic = get_ioapic_for_interrupt(irq);
  if (ioapic == NULL) {
    kprintf("no ioapic for interrupt %d", irq);
    return -ENODEV;
  }

  ioapic_rentry_t rentry = { .raw = ioapic_read64(ioapic->address, get_rentry_index(irq)) };
  rentry.dest_mode = mode;
  rentry.dest = dest;
  ioapic_write64(ioapic->address, get_rentry_index(irq), rentry.raw);
  return 0;
}

int ioapic_set_irq_mask(uint8_t irq, bool mask) {
  struct ioapic_device *ioapic = get_ioapic_for_interrupt(irq);
  if (ioapic == NULL) {
    kprintf("no ioapic for interrupt %d", irq);
    return -ENODEV;
  }

  ioapic_rentry_t rentry = { .raw = ioapic_read64(ioapic->address, get_rentry_index(irq)) };
  rentry.mask = mask & 1;
  ioapic_write64(ioapic->address, get_rentry_index(irq), rentry.raw);
  return 0;
}

//

void register_ioapic(uint8_t id, uintptr_t address, uint32_t gsi_base) {
  if (id >= MAX_IOAPICS || num_ioapics >= MAX_IOAPICS) {
    kprintf("IOAPIC: ignoring ioapic %d, not supported\n", id);
    return;
  } else if (get_ioapic_by_id(id) != NULL) {
    panic("ioapic %d already registered", id);
  }

  kprintf("registering IOAPIC[%d] address=%p GSI=%d\n", id, address, gsi_base);
  struct ioapic_device *ioapic = kmalloc(sizeof(struct ioapic_device));
  ioapic->id = id;
  ioapic->gsi_base = gsi_base;
  ioapic->phys_addr = address;
  ioapic->address = address;

  uint32_t version = ioapic_read(address, IOAPIC_VERSION);
  ioapic->max_rentry = (version >> 16) & 0xFF;
  ioapic->version = version & 0xFF;

  // mask all interrupts
  for (uint8_t i = 0; i < ioapic->max_rentry; i++) {
    ioapic_rentry_t rentry = { .raw = ioapic_read64(address, get_rentry_index(i)) };
    rentry.mask = 1;
    ioapic_write64(address, get_rentry_index(i), rentry.raw);
  }

  num_ioapics += 1;
  LIST_ADD(&ioapics, ioapic, list);
  register_init_address_space_callback(remap_ioapic_registers, ioapic);
}

void disable_legacy_pic() {
  outb(0x21, 0xFF); // legacy pic1
  outb(0xA1, 0xFF); // legacy pic2
}
