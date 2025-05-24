//
// Created by Aaron Gill-Braun on 2020-10-17.
//

#include <kernel/hw/hpet.h>

#include <kernel/alarm.h>
#include <kernel/clock.h>
#include <kernel/mm.h>
#include <kernel/irq.h>
#include <kernel/init.h>

#include <kernel/string.h>
#include <kernel/panic.h>
#include <kernel/printf.h>
#include <asm/bits.h>

#define ASSERT(x) kassert(x)
#define DPRINTF(x, ...) kprintf("hpet: " x, ##__VA_ARGS__)

#define MAX_HPETS 4

#define timer_config_reg(n) \
  (HPET_TIMER_CONFIG_BASE + (0x20 * (n)))

#define timer_value_reg(n) \
  (HPET_TIMER_VALUE_BASE + (0x20 * (n)))

#define timer_fsb_irr_reg(n) \
  (HPET_TIMER_FSB_IRR_BASE + 0x20 * (n))


// HPET config register flags
#define HPET_CLOCK_EN           0x0001ULL
#define HPET_LEGACY_ROUTE_EN    0x0002ULL
// HPET config register bits
#define HPET_ID_REV_ID(x) ((x) & 0xFF)
#define HPET_ID_TIMER_COUNT(x) (((x) >> 8) & 0x1F)
#define HPET_ID_COUNT_SIZE(x) (((x) >> 13) & 0x1)
#define HPET_ID_LEGACY_REPLACE(x) (((x) >> 14) & 0x1)
#define HPET_ID_VENDOR_ID(x) (((x) >> 16) & 0xFFFF)
#define HPET_ID_CLOCK_PERIOD(x) ((x) >> 32)

// Tn config register flags
#define HPET_TN_INT_TYPE_LEVEL  0x0002ULL
#define HPET_TN_INT_EN          0x0004ULL
#define HPET_TN_TYPE_PERIODIC   0x0008ULL
#define HPET_TN_VALUE_SET       0x0040ULL
#define HPET_TN_32BIT_MODE      0x0100ULL
#define HPET_TN_FSB_EN          0x4000ULL
#define HPET_TN_INT_ROUTE(n)    (((n) & 0x1F) << 9)
// Tn config register bits
#define HPET_TN_PER_INT_CAP(x)    (((x) >> 4) & 0x1)
#define HPET_TN_SIZE_CAP(x)       (((x) >> 5) & 0x1)
#define HPET_TN_FSB_INT_CAP(x)    (((x) >> 15) & 0x1)
#define HPET_TN_INT_ROUTE_CAP(x)  (((x) >> 32) & UINT32_MAX)

// clears all writable bits
#define HPET_TN_CONFIG_MASK 0x00008030

typedef enum hpet_reg {
  HPET_ID     = 0x000,
  HPET_CONFIG = 0x010,
  HPET_STATUS = 0x020,
  HPET_COUNT  = 0x0F0,
  HPET_TIMER_CONFIG_BASE = 0x100,
  HPET_TIMER_VALUE_BASE  = 0x108,
  HPET_TIMER_FSB_IRR_BASE = 0x110
} hpet_reg_t;


struct hpet_timer_device;

struct hpet_device {
  uint8_t id;
  uint8_t max_num_timers;
  uint8_t count_size;
  uint8_t legacy_replace;

  uint32_t min_count;
  uint32_t clock_period_ns;
  uint64_t clock_count_mask;

  uintptr_t phys_addr;
  uintptr_t address;

  LIST_HEAD(struct hpet_timer_device) timers;
  LIST_ENTRY(struct hpet_device) list;
};

struct hpet_timer_device {
  struct hpet_device *hpet;
  uint8_t num;
  LIST_ENTRY(struct hpet_timer_device) list;
};

static size_t num_hpets = 0;
static LIST_HEAD(struct hpet_device) hpets;
static struct hpet_device *global_hpet_device;

static inline uint32_t hpet_read32(uintptr_t address, hpet_reg_t reg) {
  volatile uint32_t *hpet = (uint32_t *) (address + reg);
  return *hpet;
}

static inline uint64_t hpet_read64(uintptr_t address, hpet_reg_t reg) {
  volatile uint64_t *hpet = (uint64_t *) (address + reg);
  return *hpet;
}

static inline void hpet_write32(uintptr_t address, hpet_reg_t reg, uint32_t value) {
  volatile uint32_t *hpet = (uint32_t *) (address + reg);
  *hpet = value;
}

static inline void hpet_write64(uintptr_t address, hpet_reg_t reg, uint64_t value) {
  volatile uint64_t *hpet = (uint64_t *) (address + reg);
  *hpet = value;
}

struct hpet_device *get_hpet_by_id(uint8_t id) {
  struct hpet_device *hpet;
  LIST_FOREACH(hpet, &hpets, list) {
    if (hpet->id == id) {
      return hpet;
    }
  }

  return NULL;
}

struct hpet_timer_device *get_hpet_timer_by_id(struct hpet_device *hpet, uint8_t n) {
  struct hpet_timer_device *timer;
  LIST_FOREACH(timer, &hpet->timers, list) {
    if (timer->num == n) {
      return timer;
    }
  }

  return NULL;
}

//

void remap_hpet_registers(void *data) {
  struct hpet_device *hpet = data;
  hpet->address = vmap_phys(hpet->phys_addr, 0, PAGE_SIZE, VM_WRITE|VM_NOCACHE, "hpet");
}

//////////////////////////////
// MARK: HPET clock source

int hpet_clock_enable(clock_source_t *cs) {
  struct hpet_device *hpet = cs->data;
  if (hpet == NULL) {
    return -ENODEV;
  }

  uint32_t config_reg = hpet_read32(hpet->address, HPET_CONFIG);
  config_reg |= HPET_CLOCK_EN;
  hpet_write32(hpet->address, HPET_CONFIG, config_reg);
  return 0;
}

int hpet_clock_disable(clock_source_t *cs) {
  struct hpet_device *hpet = cs->data;
  if (hpet == NULL) {
    return -ENODEV;
  }

  uint32_t config_reg = hpet_read32(hpet->address, HPET_CONFIG);
  config_reg &= ~HPET_CLOCK_EN;
  hpet_write32(hpet->address, HPET_CONFIG, config_reg);
  return 0;
}

uint64_t hpet_clock_read(clock_source_t *cs) {
  struct hpet_device *hpet = cs->data;
  if (hpet == NULL) {
    return -ENODEV;
  }

  if (hpet->count_size == 64) {
    return hpet_read64(hpet->address, HPET_COUNT);
  }
  return hpet_read32(hpet->address, HPET_COUNT);
}

//////////////////////////////
// MARK: HPET alarm source

int hpet_alarm_source_init(alarm_source_t *as, uint32_t mode, irq_handler_t handler) {
  struct hpet_timer_device *tn = as->data;
  if (tn == NULL) {
    return -ENODEV;
  }


  ASSERT(mode != 0);
  ASSERT(as->mode == 0);
  if (mode != ALARM_CAP_ONE_SHOT && mode != ALARM_CAP_PERIODIC) {
    return -EINVAL;
  }

  struct hpet_device *hpet = tn->hpet;
  uint32_t tn_config_reg = hpet_read32(hpet->address, timer_config_reg(tn->num));
  if (mode == ALARM_CAP_PERIODIC && !HPET_TN_PER_INT_CAP(tn_config_reg)) {
    DPRINTF("timer does not support periodic mode\n");
    return -EINVAL;
  }

  // clear all configurable bits
  tn_config_reg &= HPET_TN_CONFIG_MASK;

  // find a routable irq
  //   mask isa irqs (we dont support legacy replace)
  uint32_t tn_route_cap = HPET_TN_INT_ROUTE_CAP(hpet_read64(hpet->address, timer_config_reg(tn->num))) & ~0xFFFF;
  uint8_t irq;
  while (true) {
    if (tn_route_cap == 0) {
      panic("hpet: no routable interrupts");
    }

    irq = __bsf32(tn_route_cap);
    if (irq_try_reserve_irqnum(irq) < 0) {
      tn_route_cap ^= (1 << irq);
      continue;
    }
    break;
  }

  as->irq_num = irq;
  as->mode = mode;
  irq_register_handler(irq, handler, as);
  irq_enable_interrupt(irq);

  // zero comparator register
  if (HPET_TN_SIZE_CAP(tn_config_reg)) {
    as->value_mask = UINT64_MAX;
    hpet_write64(hpet->address, timer_value_reg(tn->num), 0);
  } else {
    as->value_mask = UINT32_MAX;
    hpet_write32(hpet->address, timer_value_reg(tn->num), 0);
  }

  // configure timer
  tn_config_reg |= HPET_TN_INT_ROUTE(irq);
  if (mode == ALARM_CAP_PERIODIC) {
    tn_config_reg |= HPET_TN_TYPE_PERIODIC;
  }
  hpet_write32(hpet->address, timer_config_reg(tn->num), tn_config_reg);
  return 0;
}

int hpet_alarm_source_enable(alarm_source_t *as) {
  ASSERT(as->mode != 0);
  struct hpet_timer_device *tn = as->data;
  if (tn == NULL) {
    return -ENODEV;
  }

  struct hpet_device *hpet = tn->hpet;

  uint32_t tn_config_reg = hpet_read32(hpet->address, timer_config_reg(tn->num));
  tn_config_reg |= HPET_TN_INT_EN;
  hpet_write32(hpet->address, timer_config_reg(tn->num), tn_config_reg);
  return 0;
}

int hpet_alarm_source_disable(alarm_source_t *as) {
  ASSERT(as->mode != 0);
  struct hpet_timer_device *tn = as->data;
  if (tn == NULL) {
    return -ENODEV;
  }

  struct hpet_device *hpet = tn->hpet;

  uint32_t tn_config_reg = hpet_read32(hpet->address, timer_config_reg(tn->num));
  tn_config_reg &= ~HPET_TN_INT_EN;
  hpet_write32(hpet->address, timer_config_reg(tn->num), tn_config_reg);
  return 0;
}

int hpet_alarm_source_setval(alarm_source_t *as, uint64_t value) {
  DPRINTF("setval: %llu\n", value);
  ASSERT(as->mode != 0);
  struct hpet_timer_device *tn = as->data;
  if (tn == NULL) {
    return -ENODEV;
  }

  struct hpet_device *hpet = tn->hpet;

  if (as->mode == ALARM_CAP_PERIODIC) {
    // disable hpet clock
    uint32_t config_reg = hpet_read32(hpet->address, HPET_CONFIG);
    config_reg &= ~HPET_CLOCK_EN;
    hpet_write32(hpet->address, HPET_CONFIG, config_reg);

    uint32_t tn_config_reg = hpet_read32(hpet->address, timer_config_reg(tn->num));
    tn_config_reg |= HPET_TN_VALUE_SET;
    hpet_write32(hpet->address, timer_config_reg(tn->num), tn_config_reg);
  }

  // write value
  if (as->value_mask == UINT64_MAX) {
    hpet_write64(hpet->address, timer_value_reg(tn->num), value);
  } else {
    hpet_write32(hpet->address, timer_value_reg(tn->num), value);
  }

  if (as->mode == ALARM_CAP_PERIODIC) {
    // re-enable hpet clock
    uint32_t config_reg = hpet_read32(hpet->address, HPET_CONFIG);
    config_reg |= HPET_CLOCK_EN;
    hpet_write32(hpet->address, HPET_CONFIG, config_reg);
  }

  return 0;
}

//

void register_hpet_alarm_source(struct hpet_device *hpet, uint8_t n) {
  if (get_hpet_timer_by_id(hpet, n) != NULL) {
    panic("hpet: timer %d already registered", n);
  } else if (n >= hpet->max_num_timers) {
    panic("hpet: timer %d out of range", n);
  }

  struct hpet_timer_device *hpet_timer_struct = kmallocz(sizeof(struct hpet_timer_device));
  hpet_timer_struct->hpet = hpet;
  hpet_timer_struct->num = n;

  alarm_source_t *hpet_alarm_source = kmalloc(sizeof(alarm_source_t));
  hpet_alarm_source->name = kasprintf("hpet%d", n);
  hpet_alarm_source->data = hpet_timer_struct;
  hpet_alarm_source->cap_flags = ALARM_CAP_ONE_SHOT;
  hpet_alarm_source->scale_ns = hpet->clock_period_ns;

  uint32_t tn_config_reg = hpet_read32(hpet->address, timer_config_reg(n));
  if (HPET_TN_PER_INT_CAP(tn_config_reg)) {
    hpet_alarm_source->cap_flags |= ALARM_CAP_PERIODIC;
  }

  // configure timer
  tn_config_reg |= ~(HPET_TN_INT_EN | HPET_TN_FSB_EN); // disable timer interrupts
  tn_config_reg |= HPET_TN_INT_TYPE_LEVEL;            // set edge interupt type
  hpet_write32(hpet->address, timer_config_reg(n), tn_config_reg);

  hpet_alarm_source->init = hpet_alarm_source_init;
  hpet_alarm_source->enable = hpet_alarm_source_enable;
  hpet_alarm_source->disable = hpet_alarm_source_disable;
  hpet_alarm_source->setval = hpet_alarm_source_setval;

  LIST_ADD(&hpet->timers, hpet_timer_struct, list);
  register_alarm_source(hpet_alarm_source);
}

void register_hpet(uint8_t id, uintptr_t address, uint16_t min_period) {
  if (num_hpets >= MAX_HPETS) {
    DPRINTF("ignoring hpet %d, not supported\n", id);
    return;
  } else if (get_hpet_by_id(id) != NULL) {
    panic("hpet %d already registered", id);
  }

  struct hpet_device *hpet = kmallocz(sizeof(struct hpet_device));
  hpet->id = id;
  hpet->phys_addr = address;
  hpet->address = address;

  uint64_t id_reg = hpet_read64(address, HPET_ID);
  uint64_t period_ns = FS_TO_NS(HPET_ID_CLOCK_PERIOD(id_reg));
  bool legacy_replace = HPET_ID_LEGACY_REPLACE(id_reg);

  hpet->max_num_timers = HPET_ID_TIMER_COUNT(id_reg) + 1;
  hpet->count_size = HPET_ID_COUNT_SIZE(id_reg) ? 64 : 32;
  hpet->legacy_replace = HPET_ID_LEGACY_REPLACE(id_reg);
  hpet->min_count = min_period / HPET_ID_CLOCK_PERIOD(id_reg);
  hpet->clock_period_ns = period_ns;
  hpet->clock_count_mask = hpet->count_size == 64 ? UINT64_MAX : UINT32_MAX;
  LIST_INIT(&hpet->timers);

  kprintf("HPET[%d]: %d timers, %d bits, %u ns period, rev %d [legacy replace = %d]\n",
          id, hpet->max_num_timers, hpet->count_size, hpet->clock_period_ns,
          HPET_ID_REV_ID(id_reg), legacy_replace);

  uint64_t tn_config_reg;
  for (int i = 0; i < hpet->max_num_timers; i++) {
    tn_config_reg = hpet_read64(hpet->address, timer_config_reg(i));
    kprintf("  timer %d: enabled=%d type=%d fsb delivery=%d routing=%#b [%#b]\n",
            i, (tn_config_reg & (1 << 2)) != 0, (tn_config_reg & (1 << 1)),
            (tn_config_reg & (1 << 15)) != 0, HPET_TN_INT_ROUTE_CAP(tn_config_reg), tn_config_reg);
  }

  uint32_t config_reg = hpet_read32(hpet->address, HPET_CONFIG);
  config_reg &= ~HPET_CLOCK_EN;
  hpet_write32(hpet->address, HPET_CONFIG, config_reg);

  if (hpet->count_size == 64) {
    hpet_write64(hpet->address, HPET_COUNT, 0);
  } else {
    hpet_write32(hpet->address, HPET_COUNT, 0);
  }

  if (global_hpet_device == NULL) {
    global_hpet_device = hpet;

    // register hpet as clock source
    clock_source_t *hpet_clock_source = kmallocz(sizeof(clock_source_t));
    memset(hpet_clock_source, 0, sizeof(clock_source_t));
    hpet_clock_source->name = "hpet";
    hpet_clock_source->data = hpet;
    hpet_clock_source->scale_ns = hpet->clock_period_ns;
    hpet_clock_source->last_count = hpet_read64(hpet->address, HPET_COUNT);
    hpet_clock_source->value_mask = hpet->clock_count_mask;

    hpet_clock_source->enable = hpet_clock_enable;
    hpet_clock_source->disable = hpet_clock_disable;
    hpet_clock_source->read = hpet_clock_read;

    register_clock_source(hpet_clock_source);
    register_hpet_alarm_source(hpet, 0); // register timer 0
  }

  num_hpets += 1;
  LIST_ADD(&hpets, hpet, list);
  register_init_address_space_callback(remap_hpet_registers, hpet);
}
