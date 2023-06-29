//
// Created by Aaron Gill-Braun on 2022-08-02.
//

#include <kernel/acpi/pm_timer.h>
#include <kernel/acpi/acpi.h>

#include <kernel/cpu/io.h>

#include <kernel/mm.h>
#include <kernel/init.h>
#include <kernel/clock.h>
#include <kernel/panic.h>
#include <kernel/string.h>

extern acpi_fadt_t *acpi_global_fadt;

// 3.579545 MHz
const uint64_t pm_timer_frequency = 3579545UL;
clock_source_t *pm_timer_clock_source = NULL;

int acpi_pm_timer_enable(struct clock_source *cs) {
  return 0;
}

int acpi_pm_timer_disable(struct clock_source *cs) {
  return 0;
}

uint64_t acpi_pm_timer_mem_read(struct clock_source *cs) {
  volatile uint32_t *value = cs->data;
  return *value & cs->value_mask;
}

uint64_t acpi_pm_timer_io_read(struct clock_source *cs) {
  return indw((uint16_t)((uintptr_t) cs->data)) & cs->value_mask;
}

//

void remap_pm_timer_registers(void *data) {
  clock_source_t *cs = data;
  uintptr_t phys_addr = align_down((uintptr_t) cs->data, PAGE_SIZE);
  cs->data = (void *) vm_alloc_map_phys(phys_addr, 0, PAGE_SIZE, 0, PG_NOCACHE, "pm_timer")->address;
}

//

void register_acpi_pm_timer() {
  kassert(acpi_global_fadt != NULL);

  uint64_t period_ns = 1 / ((double) pm_timer_frequency / NS_PER_SEC);

  clock_source_t *cs = kmalloc(sizeof(clock_source_t));
  memset(cs, 0, sizeof(clock_source_t));
  cs->name = "acpi_pm";
  cs->data = NULL;
  cs->scale_ns = period_ns;
  cs->last_tick = 0;
  cs->value_mask = UINT32_MAX;

  cs->enable = acpi_pm_timer_enable;
  cs->disable = acpi_pm_timer_disable;
  if (acpi_global_fadt->x_pm_tmr_blk.address != 0) {
    uint8_t type = acpi_global_fadt->x_pm_tmr_blk.address_space_id;
    if (type == 0x00) {
      // memory
      cs->read = acpi_pm_timer_mem_read;
      cs->data = (void *) acpi_global_fadt->x_pm_tmr_blk.address;
      register_init_address_space_callback(remap_pm_timer_registers, cs);
    } else if (type == 0x01) {
      // io
      cs->read = acpi_pm_timer_io_read;
      cs->data = (void *) acpi_global_fadt->x_pm_tmr_blk.address;
    }
  } else if (acpi_global_fadt->pm_tmr_blk != 0) {
    cs->read = acpi_pm_timer_io_read;
    cs->data = (void *)((uintptr_t) acpi_global_fadt->pm_tmr_blk);
  } else {
    // not supported
    kfree(cs);
    return;
  }

  cs->last_tick = cs->read(cs);
  pm_timer_clock_source = cs;
  register_clock_source(cs);
}

//

int pm_timer_udelay(clock_t us) {
  uint64_t delay_ns = US_TO_NS(us);
  uint64_t period_ns = 1 / ((double) pm_timer_frequency / NS_PER_SEC);
  uint64_t count = pm_timer_clock_source->read(pm_timer_clock_source) + (delay_ns / period_ns);
  while (pm_timer_clock_source->read(pm_timer_clock_source) < count) {
    cpu_pause();
  }
  return 0;
}

int pm_timer_mdelay(clock_t ms) {
  uint64_t delay_ns = MS_TO_NS(ms);
  uint64_t period_ns = 1 / ((double) pm_timer_frequency / NS_PER_SEC);
  uint64_t count = pm_timer_clock_source->read(pm_timer_clock_source) + (delay_ns / period_ns);
  while (pm_timer_clock_source->read(pm_timer_clock_source) < count) {
    cpu_pause();
  }
  return 0;
}
