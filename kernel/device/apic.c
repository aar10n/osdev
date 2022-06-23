//
// Created by Aaron Gill-Braun on 2020-10-14.
//

#include <device/apic.h>
#include <device/pit.h>

#include <cpu/cpu.h>
#include <mm.h>

#include <system.h>
#include <panic.h>
#include <printf.h>
#include <percpu.h>
#include <vectors.h>

#define ms_to_count(ms) (apic_clock / (US_PER_SEC / ((ms) * 1000)))
#define us_to_count(us) (apic_clock / (US_PER_SEC / (us)))

static uintptr_t apic_base;
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

static inline apic_reg_icr_t apic_read_icr() {
  apic_reg_icr_t icr;
  icr.raw_low = apic_read(APIC_ICR_LOW);
  icr.raw_high = apic_read(APIC_ICR_HIGH);
  return icr;
}

static inline void apic_write_icr(apic_reg_icr_t icr) {
  apic_write(APIC_ICR_HIGH, icr.raw_high);
  apic_write(APIC_ICR_LOW, icr.raw_low);
}

//

void get_cpu_clock() {
  kprintf("[apic] determining cpu clock speed\n");

  uint64_t ms = 5;
  uint64_t t0, t1, min;

  min = UINT64_MAX;
  for (int i = 0; i < 5; i++) {
    t0 = __read_tsc();
    pit_mdelay(ms);
    t1 = __read_tsc();

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
  kprintf("[apic] initializing\n");

  // map apic registers into virtual address space
  kprintf("[apic] mapping local apic\n");
  uintptr_t phys_addr = APIC_BASE_PA;
  uintptr_t virt_addr = (uintptr_t) _vmap_mmio(phys_addr, PAGE_SIZE, PG_WRITE | PG_NOCACHE);
  apic_base = virt_addr;

  // ensure the apic is enabled
  uint64_t apic_msr = read_msr(IA32_APIC_BASE_MSR);
  write_msr(IA32_APIC_BASE_MSR, apic_msr | (1 << 11));

  apic_reg_id_t id = { .raw = apic_read(APIC_ID) };
  kprintf("[apic] id: %d\n", id.id);
  apic_reg_version_t version = { .raw = apic_read(APIC_VERSION) };
  kprintf("[apic] version: 0x%X\n", version.version);
  kprintf("[apic] max lvt: %d\n", version.max_lvt_entry);

  apic_reg_tpr_t tpr = apic_reg_tpr(0, 0);
  apic_write(APIC_TPR, tpr.raw);

  apic_reg_ldr_t ldr = apic_reg_ldr(0xFF);
  apic_write(APIC_LDR, ldr.raw);

  apic_reg_dfr_t dfr = apic_reg_dfr(APIC_FLAT_MODEL);
  apic_write(APIC_DFR, dfr.raw);

  apic_reg_lvt_timer_t timer = apic_reg_lvt_timer(
    VECTOR_SCHED_TIMER, APIC_IDLE, APIC_MASK, APIC_ONE_SHOT
  );
  apic_write(APIC_LVT_TIMER, timer.raw);

  apic_reg_lvt_lint_t lint = apic_reg_lvt_lint(
    0, APIC_FIXED, APIC_IDLE, 0, APIC_MASK, 0, APIC_LEVEL
  );

  lint.vector = VECTOR_APIC_LINT0;
  apic_write(APIC_LVT_LINT0, lint.raw);
  lint.vector = VECTOR_APIC_LINT1;
  apic_write(APIC_LVT_LINT1, lint.raw);

  apic_reg_svr_t svr = apic_reg_svr(VECTOR_APIC_SPURIOUS, 1, 0);
  apic_write(APIC_SVR, svr.raw);

  get_cpu_clock();
  get_apic_clock();

  apic_send_eoi();
  kprintf("[apic] done!\n");
}

void apic_init_periodic(uint64_t ms) {
  apic_reg_div_config_t div = apic_reg_div_config(APIC_DIVIDE_1);
  apic_write(APIC_DIVIDE_CONFIG, div.raw);

  apic_reg_lvt_timer_t timer = apic_read_timer();
  timer.timer_mode = APIC_PERIODIC;
  timer.mask = APIC_UNMASK;
  timer.vector = VECTOR_SCHED_TIMER;
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

void apic_send_ipi(uint8_t mode, uint8_t dest_mode, uint8_t dest, uint8_t vector) {
  poll_icr_status();
  apic_reg_icr_t icr;
  icr.raw = 0;
  icr = apic_reg_icr(
    vector, mode, dest_mode, APIC_IDLE,
    APIC_ASSERT, APIC_EDGE, APIC_DEST_TARGET,
    dest
  );
  apic_write_icr(icr);
}

void apic_broadcast_ipi(uint8_t mode, uint8_t shorthand, uint8_t vector) {
  poll_icr_status();
  apic_reg_icr_t icr;
  icr.raw = 0;
  icr = apic_reg_icr(
    vector, mode, APIC_DEST_PHYSICAL, APIC_IDLE,
    APIC_ASSERT, APIC_EDGE, shorthand, 0
  );
  apic_write_icr(icr);
}

void apic_self_ipi(uint8_t mode, uint8_t vector) {
  poll_icr_status();
  apic_reg_icr_t icr = apic_reg_icr(
    vector, mode, APIC_DEST_PHYSICAL, APIC_IDLE,
    APIC_ASSERT, APIC_EDGE, APIC_DEST_SELF, 0
  );
  apic_write_icr(icr);
}

void apic_send_eoi() {
  apic_write(APIC_EOI, 0);
}
