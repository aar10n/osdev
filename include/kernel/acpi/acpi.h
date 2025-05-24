//
// Created by Aaron Gill-Braun on 2022-06-05.
//

#ifndef KERNEL_ACPI_ACPI_H
#define KERNEL_ACPI_ACPI_H

#include <kernel/acpi/tables.h>
#include <kernel/base.h>

extern uint16_t enabled_apic_count;
extern uint16_t online_capable_apic_count;
extern uint16_t total_apic_count;
extern uint8_t apic_id_map[];


void acpi_early_init();
acpi_table_header_t *acpi_locate_table(uint32_t signature);

clock_t acpi_read_pm_timer();

#endif
