//
// Created by Aaron Gill-Braun on 2020-10-17.
//

#include <panic.h>
#include <system.h>
#include <device/hpet.h>
#include <mm.h>
#include <printf.h>

uintptr_t hpet_base;
uint64_t hpet_clock;

static inline uint64_t hpet_read(hpet_reg_t reg) {
  uintptr_t addr = hpet_base + reg;
  volatile uint64_t *hpet = (uint64_t *) addr;
  return *hpet;
}

static inline void hpet_write(hpet_reg_t reg, uint64_t value) {
  uintptr_t addr = hpet_base + reg;
  volatile uint64_t *hpet = (uint64_t *) addr;
  *hpet = value;
}

static inline uint64_t hpet_current_time() {
  uintptr_t addr = hpet_base + HPET_COUNT;
  volatile uint64_t *hpet = (uint64_t *) addr;
  return *hpet * hpet_clock;
}

//

void hpet_init() {
  kprintf("[hpet] initializing\n");
  if (system_info->hpet == NULL) {
    // TODO: Use alternative
    panic("[hpet] no hpet present\n");
  }

  kprintf("[hpet] mapping hpet\n");
  unreachable;
  uintptr_t phys_addr = system_info->hpet->phys_addr;
  uintptr_t virt_addr = (uintptr_t) _vmap_mmio(phys_addr, PAGE_SIZE, PG_WRITE | PG_NOCACHE);
  system_info->hpet->virt_addr = virt_addr;
  hpet_base = virt_addr;

  hpet_reg_id_t id = { .raw = hpet_read(HPET_ID) };
  hpet_clock = id.clock_period / 1000000; // fs -> ns

  hpet_reg_timer_config_t timer = { .raw = hpet_read(timer_config_reg(0)) };
  timer.timer_mode = 0;
  timer.trigger_mode = 0;
  timer.int_enabled = 1;
  timer.set_value = 0;
  timer.int_route = 2;
  hpet_write(timer_config_reg(0), timer.raw);

  hpet_reg_config_t config = { .raw = hpet_read(HPET_CONFIG) };
  config.enabled = 1;
  config.legacy_replace = 1;
  hpet_write(HPET_CONFIG, config.raw);
  hpet_write(HPET_STATUS, ~0);
  kprintf("[hpet] done!\n");
}

void hpet_set_timer(uint64_t ns) {
  if (ns < hpet_clock) {
    ns = hpet_clock;
  }

  hpet_write(HPET_STATUS, hpet_read(HPET_STATUS) | 1);
  hpet_reg_timer_config_t timer = { .raw = hpet_read(timer_config_reg(0)) };
  timer.timer_mode = 0;
  timer.int_route = 2;
  timer.set_value = 0;
  timer.int_enabled = 1;

  hpet_write(timer_config_reg(0), timer.raw);
  hpet_write(timer_value_reg(0), ns / hpet_clock);
}

uint64_t hpet_get_time() {
  return hpet_current_time();
}
