//
// Created by Aaron Gill-Braun on 2022-07-20.
//

#ifndef KERNEL_IPI_H
#define KERNEL_IPI_H

#include <base.h>

// inter-processor interrupts and message passing

// ipi types
typedef enum ipi_type {
  IPI_PANIC,
  IPI_INVLPG,
  IPI_SCHEDULE,
  IPI_NOOP,
  //
  NUM_IPIS,
} ipi_type_t;

// ipi delivery modes
typedef enum ipi_mode {
  IPI_SELF,
  IPI_ALL_INCL,
  IPI_ALL_EXCL,
} ipi_mode_t;

typedef void (*ipi_handler_t)(uint64_t data);

int ipi_deliver_cpu_id(ipi_type_t type, uint8_t cpu_id, uint64_t data);
int ipi_deliver_mode(ipi_type_t type, ipi_mode_t mode, uint64_t data);

#endif
