//
// Created by Aaron Gill-Braun on 2022-08-02.
//

#ifndef KERNEL_ACPI_PM_TIMER_H
#define KERNEL_ACPI_PM_TIMER_H

#include <kernel/base.h>

void register_acpi_pm_timer();
int pm_timer_udelay(clock_t us);
int pm_timer_mdelay(clock_t ms);

#endif
