//
// Created by Aaron Gill-Braun on 2020-10-14.
//

#include <device/apic.h>
#include <device/pit.h>

#include <cpu/cpu.h>
#include <mm.h>
#include <init.h>

#include <panic.h>
#include <printf.h>

#define APIC_BASE_PA 0xFEE00000

#define ms_to_count(ms) (apic_clock / (US_PER_SEC / ((ms) * 1000)))
#define us_to_count(us) (apic_clock / (US_PER_SEC / (us)))

#define ICR_LOW_REG_MASK     0xFFF99000
#define ICR_HIGH_REG_MASK    0x00FFFFFF

#define ICR_VECTOR_SHIFT     0
#define ICR_DELIV_MODE_SHIFT 8
#define ICR_DEST_MODE_SHIFT  10
#define ICR_LEVEL_SHIFT      14
#define ICR_TRIG_MODE_SHIFT  15
#define ICR_DEST_SHRT_SHIFT  18
#define ICR_DEST_SHIFT       24

typedef enum apic_reg {
  APIC_ID            = 0x020,
  APIC_VERSION       = 0x030,
  APIC_TPR           = 0x080,
  APIC_APR           = 0x090,
  APIC_PPR           = 0x0A0,
  APIC_EOI           = 0x0B0,
  APIC_RRD           = 0x0C0,
  APIC_LDR           = 0x0D0,
  APIC_DFR           = 0x0E0,
  APIC_SVR           = 0x0F0,
  APIC_ERROR         = 0x280,
  APIC_LVT_CMCI      = 0x2F0,
  APIC_ICR_LOW       = 0x300,
  APIC_ICR_HIGH      = 0x310,
  APIC_LVT_TIMER     = 0x320,
  APIC_LVT_LINT0     = 0x350,
  APIC_LVT_LINT1     = 0x360,
  APIC_LVT_ERROR     = 0x370,
  APIC_INITIAL_COUNT = 0x380,
  APIC_CURRENT_COUNT = 0x390,
  APIC_DIVIDE_CONFIG = 0x3E0,
} apic_reg_t;

struct apic_device {
  uint8_t id;
  uint8_t : 8;
  uint16_t : 16;
  uintptr_t phys_addr;
  uintptr_t address;
  LIST_ENTRY(struct apic_device);
};

static size_t num_apics = 0;
static LIST_HEAD(struct apic_device) apics;

static uintptr_t apic_base = APIC_BASE_PA;
static uint64_t cpu_clock;  // ticks per second
static uint32_t apic_clock; // ticks per second

static inline uint32_t apic_read(apic_reg_t reg) {
  uintptr_t addr = apic_base + reg;
  volatile uint32_t *apic = (uint32_t *) addr;
  return *apic;
}

static inline void apic_write(apic_reg_t reg, uint32_t value) {
  uintptr_t addr = apic_base + reg;
  volatile uint32_t *apic = (uint32_t *) addr;
  *apic = value;
}

static inline volatile uint32_t *apic_reg_ptr(apic_reg_t reg) {
  uintptr_t addr = apic_base + reg;
  volatile uint32_t *ptr = (uint32_t *) addr;
  return ptr;
}

static inline apic_reg_lvt_timer_t apic_read_timer() {
  apic_reg_lvt_timer_t timer = { .raw = apic_read(APIC_LVT_TIMER) };
  return timer;
}

static inline void apic_write_timer(apic_reg_lvt_timer_t timer) {
  apic_write(APIC_LVT_TIMER, timer.raw);
}


//

void remap_apic_registers(void *data) {
  apic_base = (uintptr_t) _vmap_mmio(APIC_BASE_PA, PAGE_SIZE, PG_WRITE | PG_NOCACHE);
  _vmap_get_mapping(apic_base)->name = "apic";
}

//

void register_apic(uint8_t id) {
  apic_reg_id_t id_reg = { .raw = apic_read(APIC_ID) };
  if (id == id_reg.id) {
    register_init_address_space_callback(remap_apic_registers, NULL);
  }
}

//

void get_cpu_clock() {
  kprintf("[apic] determining cpu clock speed\n");

  uint64_t ms = 5;
  uint64_t t0, t1, min;

  min = UINT64_MAX;
  for (int i = 0; i < 5; i++) {
    t0 = cpu_read_tsc();
    pit_mdelay(ms);
    t1 = cpu_read_tsc();

    uint64_t diff = t1 - t0;
    if (diff < min) {
      min = diff;
    }
  }

  cpu_clock = min * (MS_PER_SEC / ms);
  kprintf("[apic] cpu clock ticks per second: %u\n", cpu_clock);

  double freq = cpu_clock / 1e6;
  kprintf("[apic] detected %.1f MHz cpu clock\n", freq);
}

void get_apic_clock() {
  kprintf("[apic] determining timer clock speed\n");
  apic_reg_lvt_timer_t timer = apic_read_timer();
  timer.mask = APIC_MASK;
  timer.timer_mode = APIC_ONE_SHOT;
  apic_write_timer(timer);

  apic_reg_div_config_t div = apic_reg_div_config(APIC_DIVIDE_1);
  apic_write(APIC_DIVIDE_CONFIG, div.raw);

  uint64_t ms = 5;
  uint32_t t0, t1, min;

  t0 = UINT32_MAX;
  min = UINT32_MAX;
  for (int i = 0; i < 5; i++) {
    apic_write(APIC_INITIAL_COUNT, t0);
    pit_mdelay(ms);
    t1 = apic_read(APIC_CURRENT_COUNT);

    uint32_t diff = t0 - t1;
    if (diff < min) {
      min = diff;
    }
  }

  apic_clock = min * (MS_PER_SEC / ms);
  kprintf("[apic] apic clock ticks per second: %u\n", apic_clock);

  double freq = apic_clock / 1e6;
  kprintf("[apic] detected %.1f MHz timer clock\n", freq);
}

void poll_icr_status() {
  volatile uint32_t *low = apic_reg_ptr(APIC_ICR_LOW);
  while (apic_icr_status(*low)) {
    // if icr is pending poll until it finishes
    cpu_pause();
  }
}

//

uint8_t apic_get_id() {
  apic_reg_id_t id = { .raw = apic_read(APIC_ID) };
  return id.id;
}

uint8_t apic_get_version() {
  apic_reg_version_t version = { .raw = apic_read(APIC_VERSION) };
  return version.version;
}


void apic_init() {
  // ensure the apic is enabled
  uint64_t apic_msr = cpu_read_msr(IA32_APIC_BASE_MSR);
  cpu_write_msr(IA32_APIC_BASE_MSR, apic_msr | (1 << 11));

  apic_reg_tpr_t tpr = apic_reg_tpr(0, 0);
  apic_write(APIC_TPR, tpr.raw);
  apic_reg_ldr_t ldr = apic_reg_ldr(0xFF);
  apic_write(APIC_LDR, ldr.raw);
  apic_reg_dfr_t dfr = apic_reg_dfr(APIC_FLAT_MODEL);
  apic_write(APIC_DFR, dfr.raw);

  apic_reg_lvt_timer_t timer = apic_reg_lvt_timer(
    0, APIC_IDLE, APIC_MASK, APIC_ONE_SHOT
  );
  apic_write(APIC_LVT_TIMER, timer.raw);

  apic_reg_lvt_lint_t lint = apic_reg_lvt_lint(
    0, APIC_FIXED, APIC_IDLE, 0, APIC_MASK, 0, APIC_LEVEL
  );

  lint.vector = 0;
  apic_write(APIC_LVT_LINT0, lint.raw);
  lint.vector = 0;
  apic_write(APIC_LVT_LINT1, lint.raw);

  apic_reg_svr_t svr = apic_reg_svr(0, 1, 0);
  apic_write(APIC_SVR, svr.raw);

  apic_send_eoi();
}

void apic_init_periodic(uint64_t ms) {
  apic_reg_div_config_t div = apic_reg_div_config(APIC_DIVIDE_1);
  apic_write(APIC_DIVIDE_CONFIG, div.raw);

  apic_reg_lvt_timer_t timer = apic_read_timer();
  timer.timer_mode = APIC_PERIODIC;
  timer.mask = APIC_UNMASK;
  timer.vector = 0;
  apic_write_timer(timer);

  apic_write(APIC_INITIAL_COUNT, ms_to_count(ms));
}

void apic_init_oneshot() {
  apic_reg_div_config_t div = apic_reg_div_config(APIC_DIVIDE_1);
  apic_write(APIC_DIVIDE_CONFIG, div.raw);

  apic_reg_lvt_timer_t timer = apic_read_timer();
  timer.timer_mode = APIC_ONE_SHOT;
  timer.mask = APIC_UNMASK;
  apic_write_timer(timer);
}

void apic_oneshot(uint64_t ms) {
  kassert(ms <= MS_PER_SEC);
  apic_write(APIC_INITIAL_COUNT, ms == 0 ? 0 : ms_to_count(ms));
}

void apic_udelay(uint64_t us) {
  apic_reg_lvt_timer_t timer = apic_read_timer();
  timer.timer_mode = APIC_ONE_SHOT;
  timer.mask = APIC_MASK;
  apic_write_timer(timer);
  while (us > 0) {
    uint32_t val = min(us, US_PER_SEC);
    uint32_t count = apic_clock / (US_PER_SEC / val);
    apic_write(APIC_INITIAL_COUNT, count);

    while (apic_read(APIC_CURRENT_COUNT) != 0) {
      cpu_pause();
    }
    us -= val;
  }
}

void apic_mdelay(uint64_t ms) {
  apic_udelay(ms * 1000);
}

void apic_send_eoi() {
  apic_write(APIC_EOI, 0);
}

//

void apic_broadcast_init_ipi(bool assert) {
  apic_read(APIC_ERROR);
  apic_write(APIC_ERROR, 0);

  uint32_t icr_high = apic_read(APIC_ICR_HIGH) & ICR_HIGH_REG_MASK;
  apic_write(APIC_ICR_HIGH, icr_high);

  // configure init
  uint32_t icr_low = apic_read(APIC_ICR_LOW) & ICR_LOW_REG_MASK;
  icr_low |= APIC_INIT << ICR_DELIV_MODE_SHIFT;
  icr_low |= (assert ? APIC_EDGE : APIC_LEVEL) << ICR_TRIG_MODE_SHIFT;
  icr_low |= (APIC_ASSERT & assert) << ICR_LEVEL_SHIFT;
  icr_low |= APIC_DEST_ALL_EXCL_SELF << ICR_DEST_SHRT_SHIFT;
  apic_write(APIC_ICR_LOW, icr_low);
}

void apic_send_init_ipi(uint8_t dest_id, bool assert) {
  apic_read(APIC_ERROR);
  apic_write(APIC_ERROR, 0);

  // set dest id
  uint32_t icr_high = apic_read(APIC_ICR_HIGH) & ICR_HIGH_REG_MASK;
  icr_high |= dest_id << ICR_DEST_SHIFT;
  apic_write(APIC_ICR_HIGH, icr_high);

  // configure init
  uint32_t icr_low = apic_read(APIC_ICR_LOW) & ICR_LOW_REG_MASK;
  icr_low |= APIC_INIT << ICR_DELIV_MODE_SHIFT;
  icr_low |= APIC_LEVEL << ICR_TRIG_MODE_SHIFT;
  icr_low |= (APIC_ASSERT & assert) << ICR_LEVEL_SHIFT;
  // icr_low |= APIC_DEST_ALL_EXCL_SELF << ICR_DEST_SHRT_SHIFT;

  // if (assert) {
  // }
  apic_write(APIC_ICR_LOW, icr_low);

  // wait for completion
  poll_icr_status();
}

void apic_send_startup_ipi(uint8_t dest_id, uint8_t vector) {
  apic_read(APIC_ERROR);
  apic_write(APIC_ERROR, 0);

  // set dest id
  uint32_t icr_high = apic_read(APIC_ICR_HIGH) & ICR_HIGH_REG_MASK;
  icr_high |= dest_id << ICR_DEST_SHIFT;
  apic_write(APIC_ICR_HIGH, icr_high);

  // configure sipi
  uint32_t icr_low = apic_read(APIC_ICR_LOW) & ICR_LOW_REG_MASK;
  icr_low |= vector;
  icr_low |= APIC_START_UP << ICR_DELIV_MODE_SHIFT;
  icr_low |= APIC_ASSERT << ICR_LEVEL_SHIFT;
  apic_write(APIC_ICR_LOW, icr_low);

  // wait for completion
  poll_icr_status();
}

void apic_send_ipi(uint8_t mode, uint8_t dest_mode, uint8_t dest, uint8_t vector, bool assert) {
  poll_icr_status();
  uint32_t icr_high = apic_read(APIC_ICR_HIGH) & ICR_HIGH_REG_MASK;
  icr_high |= (dest & 0xFF) << ICR_DEST_SHIFT;
  apic_write(APIC_ICR_HIGH, icr_high);

  uint32_t icr_low = apic_read(APIC_ICR_LOW) & ICR_LOW_REG_MASK;
  icr_low |= vector << ICR_VECTOR_SHIFT;
  icr_low |= (mode & 0b111) << ICR_DELIV_MODE_SHIFT;
  icr_low |= (dest_mode & 0b001) << ICR_DEST_MODE_SHIFT;
  icr_low |= (assert & 0b001) << ICR_LEVEL_SHIFT;
  icr_low |= APIC_EDGE << ICR_TRIG_MODE_SHIFT;
  apic_write(APIC_ICR_LOW, icr_low);
}

int apic_write_icr(uint32_t low, uint8_t dest_id) {
  uint32_t icr_high = apic_read(APIC_ICR_HIGH) & ICR_HIGH_REG_MASK;
  icr_high |= dest_id << 24;
  apic_write(APIC_ICR_HIGH, icr_high);

  uint32_t icr_low = apic_read(APIC_ICR_LOW) & ICR_LOW_REG_MASK;
  icr_low |= low;
  apic_write(APIC_ICR_LOW, icr_low);

  poll_icr_status();
  return 0;
}
