//
// Created by Aaron Gill-Braun on 2020-10-17.
//

#include <device/hpet.h>

#include <clock.h>
#include <timer.h>

#include <mm.h>
#include <irq.h>
#include <init.h>
#include <string.h>
#include <panic.h>
#include <printf.h>

#define MAX_HPETS 4

#define timer_config_reg(n) \
  (HPET_TIMER_CONFIG_BASE + (0x20 * (n)))

#define timer_value_reg(n) \
  (HPET_TIMER_VALUE_BASE + (0x20 * (n)))

#define timer_fsb_irr_reg(n) \
  (HPET_TIMER_FSB_IRR_BASE + 0x20 * (n))

#define HPET_ID_REV_ID(x) ((x) & 0xFF)
#define HPET_ID_TIMER_COUNT(x) (((x) >> 8) & 0x1F)
#define HPET_ID_COUNT_SIZE(x) (((x) >> 13) & 0x1)
#define HPET_ID_LEGACY_REPLACE(x) (((x) >> 14) & 0x1)
#define HPET_ID_VENDOR_ID(x) (((x) >> 16) & 0xFFFF)
#define HPET_ID_CLOCK_PERIOD(x) ((x) >> 32)

#define HPET_CONFIG_ENABLE_BIT (1 << 0)
#define HPET_CONFIG_LEGACY_REPLACE_BIT (1 << 1)

#define HPET_TIMER_STATUS_BIT(n) (1 << (n))

#define HPET_TN_INT_TYPE_CNF_SHIFT  1
#define HPET_TN_INT_TYPE_CNF_MASK   0x01
#define HPET_TN_INT_ENB_CNF_BIT     (1 << 2)
#define HPET_TN_VAL_SET_CNF_BIT     (1 << 6)
#define HPET_TN_TYPE_CNF_SHIFT      3
#define HPET_TN_TYPE_CNF_MASK       0x01
#define HPET_TN_INT_ROUTE_CNF_SHIFT 9
#define HPET_TN_INT_ROUTE_CNF_MASK  0x1F

#define HPET_TN_INT_TYPE_EDGE  0
#define HPET_TN_INT_TYPE_LEVEL 1

#define HPET_TN_TYPE_ONE_SHOT  0
#define HPET_TN_TYPE_PERIODIC  1

#define HPET_TN_PER_INT_CAP(x) (((x) >> 4) & 0x1)

typedef enum hpet_reg {
  HPET_ID     = 0x000,
  HPET_CONFIG = 0x010,
  HPET_STATUS = 0x020,
  HPET_COUNT  = 0x0F0,
  HPET_TIMER_CONFIG_BASE = 0x100,
  HPET_TIMER_VALUE_BASE  = 0x108,
  HPET_TIMER_FSB_IRR_BASE = 0x110
} hpet_reg_t;

struct hpet_device {
  uint8_t id;
  uint8_t used;
  uint8_t num_timers;
  uint8_t count_size : 7;
  uint8_t legacy_replace : 1;

  uint32_t min_count;
  uint32_t clock_period_ns;

  uintptr_t phys_addr;
  uintptr_t address;
};

struct hpet_timer_device {
  struct hpet_device *hpet;
  uint8_t num;
  uint8_t mode;
};

static size_t num_hpets = 0;
static struct hpet_device hpets[MAX_HPETS];
static int global_hpet_id = -1;


static inline uint32_t hpet_read32(uint8_t id, hpet_reg_t reg) {
  uintptr_t address = hpets[id].address;
  volatile uint32_t *hpet = (uint32_t *) (address + reg);
  return *hpet;
}

static inline uint64_t hpet_read64(uint8_t id, hpet_reg_t reg) {
  uintptr_t address = hpets[id].address;
  volatile uint64_t *hpet = (uint64_t *) (address + reg);
  return *hpet;
}

static inline void hpet_write32(uint8_t id, hpet_reg_t reg, uint32_t value) {
  uintptr_t address = hpets[id].address;
  volatile uint32_t *hpet = (uint32_t *) (address + reg);
  *hpet = value;
}

static inline void hpet_write64(uint8_t id, hpet_reg_t reg, uint64_t value) {
  uintptr_t address = hpets[id].address;
  volatile uint64_t *hpet = (uint64_t *) (address + reg);
  *hpet = value;
}

static inline uint64_t hpet_current_time(uint8_t id) {
  uintptr_t address = hpets[id].address;
  volatile uint64_t *hpet = (uint64_t *) (address + HPET_COUNT);
  return *hpet * hpets[id].clock_period_ns;
}

//

void hpet_interrupt_handler(uint8_t vector, void *data) {
  struct hpet_timer_device *tn = data;
  kprintf("HPET Interrupt (timer %d)\n", tn->num);

  uint8_t id = tn->hpet->id;
  uint32_t int_status_reg = hpet_read32(id, HPET_STATUS);
  hpet_write32(id, HPET_STATUS, int_status_reg);
}

void remap_hpet_registers(void *data) {
  struct hpet_device *hpet = data;
  hpet->address = (uintptr_t) _vmap_mmio(hpet->phys_addr, PAGE_SIZE, PG_WRITE | PG_NOCACHE);
  _vmap_get_mapping(hpet->address)->name = "hpet";
}

// HPET Clock API

int hpet_clock_enable(clock_source_t *cs) {
  struct hpet_device *hpet = cs->data;
  if (hpet == NULL) {
    return -ENODEV;
  }

  uint32_t config_reg = hpet_read32(hpet->id, HPET_CONFIG);
  config_reg |= HPET_CONFIG_ENABLE_BIT;
  hpet_write32(hpet->id, HPET_CONFIG, config_reg);
  return 0;
}

int hpet_clock_disable(clock_source_t *cs) {
  struct hpet_device *hpet = cs->data;
  if (hpet == NULL) {
    return -ENODEV;
  }

  uint32_t config_reg = hpet_read32(hpet->id, HPET_CONFIG);
  config_reg &= ~HPET_CONFIG_ENABLE_BIT;
  hpet_write32(hpet->id, HPET_CONFIG, config_reg);
  return 0;
}

uint64_t hpet_clock_read(clock_source_t *cs) {
  struct hpet_device *hpet = cs->data;
  if (hpet == NULL) {
    return -ENODEV;
  }
  return hpet_read64(global_hpet_id, HPET_COUNT);
}

// HPET Timer API

int hpet_timer_init(timer_device_t *td, uint16_t mode) {
  struct hpet_timer_device *tn = td->data;
  if (tn == NULL) {
    return -ENODEV;
  }

  uint8_t id = tn->hpet->id;
  uint32_t tn_config_reg = hpet_read32(id, timer_config_reg(tn->num));
  if (mode == TIMER_PERIODIC && HPET_TN_PER_INT_CAP(tn_config_reg)) {
    tn->mode = HPET_TN_TYPE_PERIODIC;
  } else if (mode == TIMER_ONE_SHOT) {
    tn->mode = HPET_TN_TYPE_ONE_SHOT;
  } else {
    return -EINVAL;
  }

  int timer_irq = irq_alloc_hardware_irqnum();
  kassert(timer_irq >= 0);
  td->irq = (uint8_t) timer_irq;

  tn_config_reg &= ~HPET_TN_INT_ENB_CNF_BIT;
  tn_config_reg &= ~(HPET_TN_TYPE_CNF_MASK << HPET_TN_TYPE_CNF_SHIFT);
  tn_config_reg &= ~(HPET_TN_INT_ROUTE_CNF_MASK << HPET_TN_INT_ROUTE_CNF_SHIFT);
  tn_config_reg &= ~(HPET_TN_INT_TYPE_CNF_MASK << HPET_TN_INT_TYPE_CNF_SHIFT);
  tn_config_reg |= tn->mode << HPET_TN_TYPE_CNF_SHIFT;
  tn_config_reg |= (td->irq & HPET_TN_INT_ROUTE_CNF_MASK) << HPET_TN_INT_ROUTE_CNF_SHIFT;
  tn_config_reg |= HPET_TN_INT_TYPE_LEVEL << HPET_TN_INT_TYPE_CNF_SHIFT;

  hpet_write32(id, timer_config_reg(tn->num), tn_config_reg);

  irq_register_irq_handler(timer_irq, hpet_interrupt_handler, tn);
  return 0;
}

int hpet_timer_enable(timer_device_t *td) {
  struct hpet_timer_device *tn = td->data;
  if (tn == NULL) {
    return -ENODEV;
  }

  uint8_t id = tn->hpet->id;
  hpet_write64(id, HPET_COUNT, 0);

  uint32_t tn_config_reg = hpet_read32(id, timer_config_reg(tn->num));
  tn_config_reg |= HPET_TN_INT_ENB_CNF_BIT;
  hpet_write32(id, timer_config_reg(tn->num), tn_config_reg);

  irq_enable_interrupt(td->irq);
  return 0;
}

int hpet_timer_disable(timer_device_t *td) {
  struct hpet_timer_device *tn = td->data;
  if (tn == NULL) {
    return -ENODEV;
  }

  uint8_t id = tn->hpet->id;
  uint32_t tn_config_reg = hpet_read32(id, timer_config_reg(tn->num));
  tn_config_reg &= ~HPET_TN_INT_ENB_CNF_BIT;
  hpet_write32(id, timer_config_reg(tn->num), tn_config_reg);

  irq_disable_interrupt(td->irq);
  return 0;
}

int hpet_timer_setval(timer_device_t *td, uint64_t ns) {
  struct hpet_timer_device *tn = td->data;
  if (tn == NULL) {
    return -ENODEV;
  }

  uint8_t id = tn->hpet->id;
  uint64_t period_ns = tn->hpet->clock_period_ns;
  if (tn->mode == HPET_TN_TYPE_PERIODIC) {
    uint32_t tn_config_reg = hpet_read32(id, timer_config_reg(tn->num));
    tn_config_reg |= HPET_TN_VAL_SET_CNF_BIT;
    hpet_write32(id, timer_config_reg(tn->num), tn_config_reg);
  }

  if (ns < period_ns) {
    ns = period_ns;
  }

  uint64_t ticks = ns / period_ns;
  hpet_write64(id, timer_value_reg(tn->num), ticks);
  return 0;
}

//

uint64_t hpet_get_count() {
  if (global_hpet_id < 0) {
    return 0;
  }
  return hpet_read64(global_hpet_id, HPET_COUNT);
}

uint32_t hpet_get_scale_ns() {
  if (global_hpet_id < 0) {
    return 0;
  }
  return hpets[global_hpet_id].clock_period_ns;
}

//

void register_hpet(uint8_t id, uintptr_t address, uint16_t min_period) {
  if (id >= MAX_HPETS || num_hpets >= MAX_HPETS) {
    kprintf("HPET: ignoring hpet %d, not supported\n", id);
    return;
  } else if (hpets[id].used) {
    panic("hpet %d already registered", id);
  }

  hpets[id].id = id;
  hpets[id].used = 1;
  hpets[id].phys_addr = address;
  hpets[id].address = address;

  uint64_t id_reg = hpet_read64(id, HPET_ID);
  bool legacy_replace = HPET_ID_LEGACY_REPLACE(id_reg);
  hpets[id].num_timers = HPET_ID_TIMER_COUNT(id_reg) + 1;
  hpets[id].count_size = HPET_ID_COUNT_SIZE(id_reg) ? 64 : 32;
  hpets[id].legacy_replace = HPET_ID_LEGACY_REPLACE(id_reg);
  hpets[id].min_count = min_period / HPET_ID_CLOCK_PERIOD(id_reg);
  hpets[id].clock_period_ns = HPET_ID_CLOCK_PERIOD(id_reg) / (FS_PER_SEC / NS_PER_SEC);

  kprintf("HPET[%d]: %d timers, %d bits, %u ns period, rev %d [legacy replace = %d]\n",
          id, hpets[id].num_timers, hpets[id].count_size, hpets[id].clock_period_ns,
          HPET_ID_REV_ID(id_reg), legacy_replace);

  uint64_t tn_config_reg;
  for (int i = 0; i < hpets[id].num_timers; i++) {
    tn_config_reg = hpet_read64(id, timer_config_reg(i));
    kprintf("  timer %d: enabled=%d routing=%#b\n", i, (tn_config_reg & (1 << 2)) != 0, (tn_config_reg >> 32));
  }

  hpet_write64(id, HPET_COUNT, 0);
  uint32_t config_reg = hpet_read32(id, HPET_CONFIG);
  config_reg &= ~HPET_CONFIG_ENABLE_BIT;

  if (global_hpet_id == -1) {
    global_hpet_id = id;

    if (hpets[id].legacy_replace) {
      kprintf("HPET: enabling legacy route replacement\n");
      config_reg |= HPET_CONFIG_LEGACY_REPLACE_BIT;
    }

    // register hpet as clock source
    clock_source_t *hpet_clock_source = kmalloc(sizeof(clock_source_t));
    memset(hpet_clock_source, 0, sizeof(clock_source_t));
    hpet_clock_source->name = "hpet";
    hpet_clock_source->data = &hpets[id];
    hpet_clock_source->scale_ns = hpets[id].clock_period_ns;
    hpet_clock_source->last_tick = hpet_read64(id, HPET_COUNT);

    hpet_clock_source->enable = hpet_clock_enable;
    hpet_clock_source->disable = hpet_clock_disable;
    hpet_clock_source->read = hpet_clock_read;

    register_clock_source(hpet_clock_source);

    // register hpet timer 0 as timer device
    struct hpet_timer_device *hpet_timer_struct = kmalloc(sizeof(struct hpet_timer_device));
    hpet_timer_struct->hpet = &hpets[id];
    hpet_timer_struct->num = 0;
    hpet_timer_struct->mode = 0;

    timer_device_t *hpet_timer_device = kmalloc(sizeof(timer_device_t));

    hpet_timer_device->name = "hpet";
    hpet_timer_device->data = hpet_timer_struct;
    hpet_timer_device->irq = 0;
    hpet_timer_device->flags = 0;

    uint32_t tn0_config_reg = hpet_read32(id, timer_config_reg(0));
    hpet_timer_device->flags |= TIMER_ONE_SHOT;
    if (HPET_TN_PER_INT_CAP(tn0_config_reg)) {
      hpet_timer_device->flags |= TIMER_PERIODIC;
    }

    uint64_t tn_status_reg = hpet_read64(id, HPET_STATUS);
    tn_status_reg |= HPET_TIMER_STATUS_BIT(hpet_timer_struct->num);
    hpet_write64(id, HPET_STATUS, tn_status_reg);

    hpet_timer_device->init = hpet_timer_init;
    hpet_timer_device->enable = hpet_timer_enable;
    hpet_timer_device->disable = hpet_timer_disable;
    hpet_timer_device->setval = hpet_timer_setval;

    register_timer_device(hpet_timer_device);
  }

  hpet_write32(id, HPET_CONFIG, config_reg);
  num_hpets += 1;
  register_init_address_space_callback(remap_hpet_registers, &hpets[id]);
}
