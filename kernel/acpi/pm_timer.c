//
// Created by Aaron Gill-Braun on 2022-08-02.
//

#include <acpi/pm_timer.h>
#include <acpi/acpi.h>

#include <cpu/io.h>

#include <mm.h>
#include <init.h>
#include <clock.h>
#include <panic.h>
#include <string.h>

extern acpi_fadt_t *acpi_global_fadt;

// 3.579545 MHz
const uint64_t pm_timer_frequency = 3579545UL;

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
  cs->data = _vmap_mmio((uintptr_t) cs->data, PAGE_SIZE, PG_NOCACHE);
  _vmap_get_mapping((uintptr_t) cs->data)->name = "pm_timer";
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
  register_clock_source(cs);
}
